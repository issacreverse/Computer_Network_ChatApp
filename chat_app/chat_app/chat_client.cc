// 브라우저(WebSocket)와 채팅 서버(TCP) 사이를 중개하는 C++ 클라이언트.
// 프로세스 하나가 로컬 포트 하나를 맡아 계속 떠 있으면서, 그 포트로 브라우저가
// (재)접속할 때마다 채팅 서버와의 TCP 연결에 중계해준다. 브라우저 탭을
// 새로고침해도(=WS가 끊겼다 다시 붙어도) 브리지 프로세스 자체는 죽지 않는다.
//
// 스레드 구성:
//   메인 스레드            : 로컬 HTTP/WS 포트 accept 루프, 연결마다 처리 스레드 생성
//   handle_browser_connection(연결마다): HTTP 요청 처리 -> 정적 파일 서빙 또는
//                             WS 핸드셰이크 후 "브라우저 -> 서버" 중계 루프로 전환.
//                             이 연결이 끊기면 스레드만 정리되고 프로세스는 유지된다.
//   server_to_browser_relay (1개, detached): 채팅 서버 -> 현재 붙어있는 브라우저로 중계.
//                             붙어있는 브라우저가 없으면 그냥 버린다(백로그 없음).
//
// 공유 상태: g_browser_ws_sock은 g_browser_mtx로, g_server_sock은 g_server_mtx로
// 보호한다. g_server_sock은 채팅 서버 연결이 끊길 때마다(강퇴, 서버 재시작 등)
// server_to_browser_relay가 재연결해서 새 값으로 바꿔치기하므로 더 이상
// "시작 시 한 번만 쓰는 읽기 전용" 값이 아니다.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <chrono>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <netdb.h>

#include "chat_protocol.h"
#include "ws_handshake.h"

static int g_server_sock = -1;
static std::mutex g_server_mtx;
static std::mutex g_browser_mtx;
static int g_browser_ws_sock = -1;
static std::string g_webroot = "web";
static std::string g_host;
static int g_port = 0;

// 업로드된 파일의 실제 바이트를 두는 디렉터리. 브릿지 프로세스(포트)가
// 여러 개 떠 있어도 전부 같은 호스트의 같은 상대 경로를 쓰므로, 어느
// 브릿지로 업로드했든 다른 브릿지가 그대로 서빙할 수 있다.
static std::string g_uploads_dir = "uploads";

// 첨부파일 업로드 용량 상한(바이트). 서버가 방을 켜둔 동안 디스크에
// 계속 쌓이는 구조라 너무 크게 잡지 않는다.
static const long MAX_UPLOAD_BYTES = 20L * 1024 * 1024;

static void die(const char* msg)
{
    perror(msg);
    exit(1);
}

// --- 채팅 서버로의 TCP 연결 ---

static int connect_to_server(const std::string& host, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) die("socket");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
    {
        struct hostent* he = gethostbyname(host.c_str());
        if (!he) die("gethostbyname");
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) die("connect");
    return sock;
}

// 채팅 서버 연결이 끊긴 뒤 재연결을 시도한다. connect_to_server와 달리 실패해도
// die()로 죽지 않고, 서버가 재시작 중일 수도 있으니 될 때까지 잠깐씩 쉬며
// 계속 시도한다 - 이 함수가 죽어버리면 브리지 프로세스 전체가 죽어서
// 브라우저가 새로고침해도 페이지 자체를 못 받아온다.
static int reconnect_to_server(const std::string& host, int port)
{
    while (true)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0)
        {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);

            bool addr_ok = true;
            if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
            {
                struct hostent* he = gethostbyname(host.c_str());
                if (he) memcpy(&addr.sin_addr, he->h_addr, he->h_length);
                else addr_ok = false;
            }

            if (addr_ok && connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0)
                return sock;

            close(sock);
        }
        sleep(1);
    }
}

// --- WebSocket 프레임 (RFC 6455, 텍스트 프레임 기준 MVP 구현) ---

static bool ws_read_text_frame(int fd, std::string& out_payload, bool& is_close)
{
    is_close = false;
    uint8_t hdr[2];
    if (recv(fd, hdr, 2, MSG_WAITALL) != 2) return false;

    int opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;

    if (len == 126)
    {
        uint8_t ext[2];
        if (recv(fd, ext, 2, MSG_WAITALL) != 2) return false;
        len = (uint16_t(ext[0]) << 8) | ext[1];
    }
    else if (len == 127)
    {
        return false; // 초대형 프레임은 MVP 범위 밖
    }

    uint8_t mask_key[4] = {0, 0, 0, 0};
    if (masked)
    {
        if (recv(fd, mask_key, 4, MSG_WAITALL) != 4) return false;
    }

    std::string payload(len, '\0');
    if (len > 0)
    {
        if (recv(fd, &payload[0], len, MSG_WAITALL) != (ssize_t)len) return false;
        if (masked)
        {
            for (uint64_t i = 0; i < len; i++)
                payload[i] = payload[i] ^ mask_key[i % 4];
        }
    }

    if (opcode == 0x8) { is_close = true; return true; }
    if (opcode == 0x1) { out_payload = payload; return true; }

    // ping/binary 등은 MVP 범위 밖: 무시하고 루프는 계속 돈다.
    out_payload.clear();
    return true;
}

static bool ws_write_text_frame(int fd, const std::string& payload)
{
    std::string frame;
    frame += char(0x81); // FIN=1, opcode=text

    size_t len = payload.size();
    if (len <= 125)
    {
        frame += char((uint8_t)len);
    }
    else if (len <= 0xFFFF)
    {
        frame += char(126);
        frame += char((len >> 8) & 0xFF);
        frame += char(len & 0xFF);
    }
    else
    {
        return false; // MVP 범위 밖
    }

    frame += payload;
    ssize_t n = write(fd, frame.data(), frame.size());
    return n == (ssize_t)frame.size();
}

// --- 최소 HTTP 파싱/서빙 ---

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string ws_key;    // Sec-WebSocket-Key (있으면 업그레이드 요청)
    bool is_upgrade = false;
    long content_length = -1; // 없으면 -1 (파일 업로드 POST에서만 쓴다)
    std::string body_prefix;  // 헤더를 읽는 도중 소켓에서 이미 딸려온 바디의 앞부분
};

static bool parse_http_request(int fd, HttpRequest& req)
{
    std::string data;
    char buf[512];
    size_t header_end;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos)
    {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) return false;
        data.append(buf, n);
        if (data.size() > 8192) return false; // 헤더가 비정상적으로 큼
    }

    // "\r\n\r\n" 이후로 이미 읽혀버린 바이트는 body의 시작 부분이다(POST일 때만
    // 의미가 있다) - 헤더 줄 파싱은 그 앞부분(header_part)만 대상으로 한다.
    std::string header_part = data.substr(0, header_end);
    req.body_prefix = data.substr(header_end + 4);

    std::istringstream iss(header_part);
    std::string line;
    std::getline(iss, line);
    {
        std::istringstream reqline(line);
        reqline >> req.method >> req.path;
    }

    while (std::getline(iss, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && val.front() == ' ') val.erase(0, 1);

        if (key == "Upgrade" && val.find("websocket") != std::string::npos)
            req.is_upgrade = true;
        else if (key == "Sec-WebSocket-Key")
            req.ws_key = val;
        else if (key == "Content-Length")
        {
            char* end = nullptr;
            long v = strtol(val.c_str(), &end, 10);
            req.content_length = (end != val.c_str()) ? v : -1;
        }
    }

    return true;
}

static std::string content_type_for(const std::string& path)
{
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html; charset=utf-8";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js")   return "application/javascript; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css")  return "text/css; charset=utf-8";
    return "application/octet-stream";
}

static void serve_static_file(int fd, std::string path)
{
    if (path == "/") path = "/chat.html";
    if (path.find("..") != std::string::npos) // 상위 디렉터리 접근 금지
    {
        std::string body = "400 Bad Request";
        std::string resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;
        if (write(fd, resp.data(), resp.size()) < 0) { /* best-effort error response */ }
        return;
    }

    std::ifstream f(g_webroot + path, std::ios::binary);
    if (!f)
    {
        std::string body = "404 Not Found";
        std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;
        if (write(fd, resp.data(), resp.size()) < 0) { /* best-effort error response */ }
        return;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string body = ss.str();

    // 개발 중 파일을 자주 바꾸므로 브라우저가 절대 캐시하지 않도록 명시한다.
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type_for(path) +
        "\r\nCache-Control: no-store\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n";
    if (write(fd, resp.data(), resp.size()) < 0) return;
    if (write(fd, body.data(), body.size()) < 0) { /* best-effort */ }
}

// --- 파일 업로드/다운로드 ---

static void send_http_error(int fd, int code, const std::string& status, const std::string& body)
{
    std::string resp = "HTTP/1.1 " + std::to_string(code) + " " + status +
        "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n" + body;
    if (write(fd, resp.data(), resp.size()) < 0) { /* best-effort error response */ }
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// application/x-www-form-urlencoded 스타일 디코딩("%XX", "+" -> 공백). 쿼리
// 파라미터로 넘어오는 방 이름/원본 파일명(둘 다 한글/공백 포함 가능)에 쓴다.
static std::string url_decode(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            int hi = hex_val(s[i + 1]);
            int lo = hex_val(s[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out += char((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += (s[i] == '+') ? ' ' : s[i];
    }
    return out;
}

static std::string get_query_param(const std::string& query, const std::string& key)
{
    size_t pos = 0;
    while (pos < query.size())
    {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key)
            return url_decode(pair.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

// 파일 id는 순전히 우리가 매기는 내부 식별자(32자리 hex)라, 업로드된 파일
// 이름과 무관하게 항상 고정 형식이다 - 그래서 /files/<id> 요청을 받았을 때
// 형식만 봐도 경로 조작 시도를 걸러낼 수 있다.
static std::string generate_file_id()
{
    static thread_local std::mt19937_64 gen(
        std::random_device{}() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<uint64_t> dist;
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx",
        (unsigned long long)dist(gen), (unsigned long long)dist(gen));
    return std::string(buf);
}

// 원본 파일명은 디스크 경로의 일부로 들어가므로, 경로 구분자/제어문자를
// 제거하고 길이를 제한한다("uploads/<fileId>__<safe_name>"로 저장).
static std::string sanitize_upload_filename(const std::string& raw)
{
    std::string out;
    for (unsigned char c : raw)
    {
        if (c == '/' || c == '\\' || c < 0x20) continue;
        out += (char)c;
        if (out.size() >= 150) break;
    }
    if (out.empty()) out = "file";
    return out;
}

// HTTP 헤더 값(Content-Disposition의 filename)에 그대로 넣을 것이므로,
// 헤더를 깨뜨리거나 주입할 수 있는 문자를 제거한다.
static std::string http_header_safe(const std::string& raw)
{
    std::string out;
    for (char c : raw)
    {
        if (c == '"' || c == '\r' || c == '\n') continue;
        out += c;
    }
    return out;
}

static std::string content_type_for_download(const std::string& name)
{
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    auto ends_with = [&](const char* ext) {
        size_t len = strlen(ext);
        return lower.size() >= len && lower.compare(lower.size() - len, len, ext) == 0;
    };

    if (ends_with(".png")) return "image/png";
    if (ends_with(".jpg") || ends_with(".jpeg")) return "image/jpeg";
    if (ends_with(".gif")) return "image/gif";
    if (ends_with(".webp")) return "image/webp";
    if (ends_with(".svg")) return "image/svg+xml";
    if (ends_with(".pdf")) return "application/pdf";
    if (ends_with(".txt")) return "text/plain; charset=utf-8";
    if (ends_with(".mp4")) return "video/mp4";
    if (ends_with(".mp3")) return "audio/mpeg";
    if (ends_with(".zip")) return "application/zip";
    return "application/octet-stream";
}

// POST /upload?room=...&filename=... : 요청 바디(파일 원본 바이트)를 통째로
// 받아 디스크에 저장하고, 채팅 서버에는 메타데이터만 FILE 메시지로 알린다.
static void handle_upload(int fd, const HttpRequest& req, const std::string& query)
{
    std::string room_name = get_query_param(query, "room");
    std::string original_name = get_query_param(query, "filename");

    if (room_name.empty() || original_name.empty())
    {
        send_http_error(fd, 400, "Bad Request", "room/filename이 필요합니다.");
        return;
    }
    if (req.content_length < 0)
    {
        send_http_error(fd, 411, "Length Required", "Content-Length가 필요합니다.");
        return;
    }
    if (req.content_length == 0)
    {
        send_http_error(fd, 400, "Bad Request", "빈 파일은 업로드할 수 없습니다.");
        return;
    }
    if (req.content_length > MAX_UPLOAD_BYTES)
    {
        send_http_error(fd, 413, "Payload Too Large", "파일이 너무 큽니다. 최대 20MB까지 업로드할 수 있습니다.");
        return;
    }

    // 헤더를 읽는 도중 소켓에서 이미 딸려온 바디 조각(body_prefix)부터 이어
    // 붙이고, Content-Length만큼 채워질 때까지 마저 읽는다.
    std::string body = req.body_prefix;
    body.reserve((size_t)req.content_length);
    while ((long)body.size() < req.content_length)
    {
        char buf[READ_CHUNK];
        size_t want = std::min((size_t)(req.content_length - (long)body.size()), sizeof(buf));
        ssize_t n = read(fd, buf, want);
        if (n <= 0)
        {
            send_http_error(fd, 400, "Bad Request", "업로드 중 연결이 끊겼습니다.");
            return;
        }
        body.append(buf, n);
    }
    if ((long)body.size() > req.content_length) body.resize((size_t)req.content_length);

    std::string safe_name = sanitize_upload_filename(original_name);
    std::string file_id = generate_file_id();
    std::string disk_path = g_uploads_dir + "/" + file_id + "__" + safe_name;

    std::ofstream out(disk_path, std::ios::binary);
    if (!out)
    {
        send_http_error(fd, 500, "Internal Server Error", "파일을 저장하지 못했습니다.");
        return;
    }
    out.write(body.data(), (std::streamsize)body.size());
    out.close();

    // 실제 바이트는 이미 디스크에 있으니, 채팅 서버에는 다른 메시지들과
    // 똑같이 취급될 수 있도록 메타데이터만 알린다(안읽음/미리보기/히스토리
    // 등 기존 인프라를 그대로 재사용) - g_server_sock은 브라우저->서버 중계
    // 스레드와 공유되므로, 한 줄이 다른 쓰기와 섞이지 않도록 쓰는 동안
    // 계속 락을 들고 있는다.
    {
        std::lock_guard<std::mutex> lock(g_server_mtx);
        write_line(g_server_sock, "FILE|" + room_name + "|" + file_id + "|" +
            std::to_string(body.size()) + "|" + safe_name);
    }

    std::string resp_body = "{\"ok\":true}";
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
        std::to_string(resp_body.size()) + "\r\nConnection: close\r\n\r\n" + resp_body;
    if (write(fd, resp.data(), resp.size()) < 0) { /* best-effort */ }
}

// GET /files/<fileId> : 업로드 당시 만들어둔 "<fileId>__<원본파일명>"과
// prefix가 일치하는 디스크 파일을 찾아 그대로 서빙한다. 어느 브릿지로
// 업로드했든 uploads/ 디렉터리를 공유하므로 이 브릿지가 아니어도 서빙된다.
static void serve_uploaded_file(int fd, const std::string& file_id)
{
    if (file_id.size() != 32 || file_id.find_first_not_of("0123456789abcdef") != std::string::npos)
    {
        send_http_error(fd, 400, "Bad Request", "잘못된 파일 id입니다.");
        return;
    }

    DIR* dir = opendir(g_uploads_dir.c_str());
    if (!dir)
    {
        send_http_error(fd, 404, "Not Found", "파일을 찾을 수 없습니다.");
        return;
    }
    std::string prefix = file_id + "__";
    std::string matched_name;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        if (name.rfind(prefix, 0) == 0) { matched_name = name; break; }
    }
    closedir(dir);

    if (matched_name.empty())
    {
        send_http_error(fd, 404, "Not Found", "파일을 찾을 수 없습니다.");
        return;
    }

    std::string display_name = matched_name.substr(prefix.size());
    std::ifstream f(g_uploads_dir + "/" + matched_name, std::ios::binary);
    if (!f)
    {
        send_http_error(fd, 404, "Not Found", "파일을 찾을 수 없습니다.");
        return;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string body = ss.str();

    // inline으로 줘도(다운로드 강제 X) 브라우저의 <img>/<iframe>이 미리보기로
    // 바로 열 수 있다 - 실제 "다운로드" 버튼은 클라이언트가 <a download> 속성으로
    // 강제한다.
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type_for_download(display_name) +
        "\r\nContent-Disposition: inline; filename=\"" + http_header_safe(display_name) + "\"" +
        "\r\nCache-Control: no-store\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n";
    if (write(fd, resp.data(), resp.size()) < 0) return;
    if (write(fd, body.data(), body.size()) < 0) { /* best-effort */ }
}

static bool do_ws_handshake(int fd, const HttpRequest& req)
{
    std::string accept_key = compute_ws_accept_key(req.ws_key);
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";
    ssize_t n = write(fd, resp.data(), resp.size());
    return n == (ssize_t)resp.size();
}

// --- 연결마다 하나: 브라우저 -> 서버 방향 ---

static void handle_browser_connection(int fd)
{
    HttpRequest req;
    if (!parse_http_request(fd, req))
    {
        close(fd);
        return;
    }

    if (!req.is_upgrade || req.ws_key.empty())
    {
        size_t q = req.path.find('?');
        std::string base_path = (q == std::string::npos) ? req.path : req.path.substr(0, q);
        std::string query = (q == std::string::npos) ? "" : req.path.substr(q + 1);

        if (req.method == "POST" && base_path == "/upload")
            handle_upload(fd, req, query);
        else if (req.method == "GET" && base_path.rfind("/files/", 0) == 0)
            serve_uploaded_file(fd, base_path.substr(7));
        else
            serve_static_file(fd, base_path);
        close(fd);
        return;
    }

    if (!do_ws_handshake(fd, req))
    {
        close(fd);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_browser_mtx);
        g_browser_ws_sock = fd;
    }

    while (true)
    {
        std::string payload;
        bool is_close = false;
        if (!ws_read_text_frame(fd, payload, is_close) || is_close)
            break;
        if (!payload.empty())
        {
            // g_server_sock은 채팅 서버가 끊겼다 재연결되면 값이 바뀔 수 있으므로
            // g_server_mtx 아래에서 읽는다(그냥 읽으면 TSan이 잡아낼 데이터 경합).
            int sock;
            { std::lock_guard<std::mutex> lock(g_server_mtx); sock = g_server_sock; }
            write_line(sock, payload);
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_browser_mtx);
        if (g_browser_ws_sock == fd) g_browser_ws_sock = -1;
    }
    close(fd);

    // 브라우저 연결이 끊겨도(새로고침 포함) 브리지 프로세스와 채팅 서버 TCP
    // 연결은 그대로 유지한다 - 다음 accept()에서 새 브라우저 연결을 받아
    // 다시 중계를 시작할 수 있어야 새로고침이 정상 동작한다.
    // g_server_sock을 여기서 닫지 않는 이유는 그 소켓을 server_to_browser_relay
    // 스레드가 read_line()으로 계속 블로킹 상태로 쓰고 있기 때문이기도 하다
    // (예전엔 여기서 close+exit 했다가 TSan이 그 스레드와의 경합을 잡아낸 적 있음).
}

// --- 1개(detached): 서버 -> 브라우저 방향 ---

static void server_to_browser_relay()
{
    while (true)
    {
        int sock;
        { std::lock_guard<std::mutex> lock(g_server_mtx); sock = g_server_sock; }

        std::string carry;
        std::string line;
        while (read_line(sock, carry, line))
        {
            std::lock_guard<std::mutex> lock(g_browser_mtx);
            if (g_browser_ws_sock != -1)
                ws_write_text_frame(g_browser_ws_sock, line);
        }

        // 채팅 서버와의 연결이 끊겼다(강퇴로 인한 서버 측 강제 종료, 서버 재시작
        // 등). 브리지 프로세스 자체는 살려둔 채 재연결한다 - 예전엔 여기서
        // exit(0)했는데, 그러면 로컬 HTTP/WS 리스너까지 같이 죽어버려서 브라우저가
        // 새로고침해도 페이지 자체를 못 받아왔다(로비로 돌아가야 할 강퇴가 세션
        // 전체를 끝장내버리는 부작용). 재연결한 새 서버 연결은 당연히 로비
        // 상태에서 시작하므로, 다음 HELLO에 자연스럽게 로비 화면이 뜬다.
        fprintf(stderr, "chat server disconnected, reconnecting...\n");
        close(sock);
        int new_sock = reconnect_to_server(g_host, g_port);
        {
            std::lock_guard<std::mutex> lock(g_server_mtx);
            g_server_sock = new_sock;
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "Usage: %s <chat_server_host> <chat_server_port> <local_http_port> [webroot]\n", argv[0]);
        return 1;
    }

    std::string host = argv[1];
    int server_port = atoi(argv[2]);
    int local_port = atoi(argv[3]);
    if (argc >= 5) g_webroot = argv[4];

    g_host = host;
    g_port = server_port;
    g_server_sock = connect_to_server(host, server_port);

    mkdir(g_uploads_dir.c_str(), 0755); // 이미 있으면 EEXIST - 무시해도 된다(다른 브릿지가 먼저 만들어뒀을 수 있음)

    std::thread relay_thread(server_to_browser_relay);
    relay_thread.detach();

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) die("socket");

    int optval = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(local_port);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) die("bind");
    if (listen(listen_sock, 8) < 0) die("listen");

    printf("chat_client bridge listening on http://localhost:%d (webroot=%s)\n", local_port, g_webroot.c_str());

    while (true)
    {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int fd = accept(listen_sock, (struct sockaddr*)&cli_addr, &cli_len);
        if (fd < 0)
        {
            perror("accept");
            continue;
        }
        std::thread t(handle_browser_connection, fd);
        t.detach();
    }

    return 0;
}
