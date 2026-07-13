//표준 헤더(운영체제 상관없이 사용 가능)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//리눅스 헤더
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <string>
#include <ctime>
#include <vector>
#include <utility>

#include "chat_protocol.h"

#define MAX_ROOMS 32
#define MAX_ROOM_NAME_LEN 40

// 한 사람이 여러 방에 동시에 속할 수 있다(로비의 "내 채팅방" 기능). 지금
// 브라우저 화면에 실제로 띄워놓고 보는 중인 방만 "활성(is_active)"이고,
// 나머지는 "백그라운드" - 백그라운드 방의 이벤트는 그대로 다 전송하지 않고
// unread_count/last_preview만 갱신한 뒤 가벼운 ROOMUPDATE만 보낸다(자세한
// 내용은 notify_room 참고).
struct Client
{
    int         sock;
    char        name[MAX_USERNAME_LEN];
    time_t      mute_until;     // 이 시각까지는 채팅 금지. 0(디폴트)이면 뮤트 아님.
    std::string color;          // 닉네임 색상 이름(예: "blue"). 빈 문자열이면 커스텀 색상 없음.
    bool        is_active;      // 지금 이 방을 화면에 띄워놓고 보는 중인가.
    int         unread_count;   // 백그라운드 상태에서 쌓인 안읽음 개수.
    std::string last_preview;   // "내 채팅방" 목록에 보여줄 마지막 메시지/알림 미리보기.
};

// 이 방에서 오간 메시지 기록. 수정/삭제 권한 확인("이 id를 누가 보냈는가")
// 뿐 아니라, 방을 잠시 비웠다가(백그라운드) 돌아오거나 새로 들어온 사람에게
// 과거 내용을 다시 보여주기(HISTORY) 위해 실제 텍스트까지 그대로 들고 있다.
// deleted/edited는 항상 최신 상태만 반영한다 - 수정 이력을 여러 개 쌓아두지
// 않고 text를 새 내용으로 덮어쓴다.
struct MessageRecord
{
    int         id;
    std::string sender;
    bool        deleted;
    bool        edited;
    bool        is_reply;
    std::string text;         // 현재 내용(삭제됐으면 의미 없음, 수정됐으면 최신 내용).
                               // is_file이면 원본 파일명을 대신 담는다(별도 필드를 안 둠).
    std::string orig_sender;  // is_reply일 때만 사용
    std::string orig_preview; // is_reply일 때만 사용
    bool        is_file;      // 첨부 파일 메시지인가(수정 불가, chat_client가 실제 바이트를 보관).
    std::string file_id;      // chat_client가 매긴 업로드 파일 식별자. is_file일 때만 사용.
    long        file_size;    // 바이트 단위. is_file일 때만 사용.
    std::vector<std::pair<std::string, std::string>> reactions; // (사용자, 이모티콘). 사용자당 최대 1개.
};

// 채팅방 하나. clients[]/clnt_id/messages/next_msg_id는 그 방 자신의 lock으로
// 보호하고, in_use·name·password·is_public 같은 "방 자체의 존재"는 전역
// rooms_lock으로 보호한다 - 두 락을 동시에 잡지 않도록 어디서든 순서를
// 지킨다(rooms_lock을 먼저 잡고 완전히 풀고 나서만 room.lock을 잡는다, 절대
// 중첩하지 않는다).
struct Room
{
    bool                       in_use;
    std::string                name;
    std::string                password;  // 빈 문자열이면 비밀번호 없음
    bool                       is_public;
    Client                     clients[MAX_CLIENT];
    int                        clnt_id;
    std::vector<MessageRecord> messages;    // 수정/삭제 권한 확인용 소유자 기록
    int                        next_msg_id; // 1부터 시작, 방마다 독립적으로 증가
    int                        pinned_msg_id; // 상단 고정된 메시지 id, 없으면 -1
    pthread_mutex_t            lock;
};

Room rooms[MAX_ROOMS];
pthread_mutex_t rooms_lock = PTHREAD_MUTEX_INITIALIZER;

// 클라이언트 팔레트(web/chat.js의 COLOR_HEX)와 이름이 정확히 일치해야 한다.
static const char* COLOR_PALETTE[] = {
    "red", "orange", "yellow", "green", "teal", "blue", "purple", "pink", "gray"
};

static bool is_valid_color(const std::string& name)
{
    for (const char* c : COLOR_PALETTE)
        if (name == c) return true;
    return false;
}

// 메시지 반응으로 허용하는 이모티콘 세트(web/chat.js의 REACTION_EMOJIS와
// 정확히 일치해야 한다). 임의의 텍스트가 반응으로 들어오는 것을 막는다.
static const char* REACTION_EMOJIS[] = {
    "👍", "❤️", "😂", "😮", "😢", "🙏"
};

static bool is_valid_reaction_emoji(const std::string& e)
{
    for (const char* r : REACTION_EMOJIS)
        if (e == r) return true;
    return false;
}

void error_handling(const char* message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(-1);
}

// room_id 방에서 exclude_sock을 제외한 모든 접속자에게, 활성/백그라운드 상관없이
// 무조건 한 줄(line)을 보낸다. EDIT/DELETE처럼 "이미 보낸 메시지에 대한 정정"
// 성격의 이벤트에 쓴다 - 백그라운드 상태에서 놓쳐도 치명적이지 않고, 굳이
// unread 카운트를 새로 늘릴 필요는 없는 것들.
void broadcast_line(int room_id, int exclude_sock, const std::string& line)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (room.clients[i].sock != exclude_sock)
            write_line(room.clients[i].sock, line);
    }
    pthread_mutex_unlock(&room.lock);
}

static std::string sanitize_field(const std::string& raw, int max_len);

// room_id 방의 이벤트를 멤버들에게 알린다. 지금 그 방을 보고 있는(is_active)
// 사람에게는 full_line을 그대로 보내고, 안 보고 있는 사람에게는 unread_count를
// 늘리고 last_preview를 갱신한 뒤 가벼운
// "ROOMUPDATE|방이름|안읽음수|인원수|메시지여부|메시지id|미리보기"만 보낸다 -
// 로비의 "내 채팅방" 목록이 실시간으로 갱신되게 하기 위함이다. is_chat_message는
// 실제 채팅(MSG/REPLY)인지, 입장/퇴장/방장변경 같은 시스템 알림인지 구분한다
// - 클라이언트는 전자일 때만 화면 상단에 알람 팝업을 띄우고, msg_id로 그
// 팝업을 눌렀을 때 해당 메시지로 바로 통통 튀며 이동한다(시스템 알림이면
// msg_id는 그냥 -1).
static void notify_room(int room_id, int exclude_sock, const std::string& full_line, const std::string& preview,
                         bool is_chat_message, int msg_id = -1)
{
    Room& room = rooms[room_id];
    std::vector<int> active_socks;
    std::vector<std::pair<int, int>> background_updates; // (소켓, 갱신된 안읽음 수)

    pthread_mutex_lock(&room.lock);
    std::string room_name = room.name;
    int member_count = room.clnt_id;
    std::string clean_preview = sanitize_field(preview, 200); // "|"/개행 제거 - 다른 방 정보와 한 줄에 나열되므로 반드시 필요
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (room.clients[i].sock == exclude_sock) continue;
        if (room.clients[i].is_active)
        {
            active_socks.push_back(room.clients[i].sock);
        }
        else
        {
            room.clients[i].unread_count++;
            room.clients[i].last_preview = clean_preview;
            background_updates.push_back({ room.clients[i].sock, room.clients[i].unread_count });
        }
    }
    pthread_mutex_unlock(&room.lock);

    for (int sock : active_socks)
        write_line(sock, full_line);
    for (auto& su : background_updates)
        write_line(su.first, "ROOMUPDATE|" + room_name + "|" + std::to_string(su.second) + "|" +
            std::to_string(member_count) + "|" + (is_chat_message ? "1" : "0") + "|" +
            std::to_string(msg_id) + "|" + clean_preview);
}

// 메시지 미리보기는 너무 길면 잘라서 보여준다.
static std::string truncate_preview(const std::string& text, size_t max_len = 40)
{
    if (text.size() <= max_len) return text;
    return text.substr(0, max_len) + "...";
}

// clnt_sock 자신을 제외한, room_id 방의 현재 접속자 명단을 clnt_sock에게만 보낸다.
static void send_roster(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    std::string roster = "ROSTER|" + room.name;
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (room.clients[i].sock != clnt_sock)
            roster += "|" + std::string(room.clients[i].name);
    }
    pthread_mutex_unlock(&room.lock);
    write_line(clnt_sock, roster);
}

// 방장 = 그 방에 남아있는 사람 중 가장 먼저 들어온 사람. clients[]가 항상
// 입장 순서를 유지하므로(뒤 항목을 당겨서 채우는 방식으로 제거), 방장은
// 그냥 항상 clients[0]이다 - 별도 변수로 추적할 필요가 없다.
static bool client_is_owner(int room_id, int sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    bool owner = (room.clnt_id > 0 && room.clients[0].sock == sock);
    pthread_mutex_unlock(&room.lock);
    return owner;
}

static bool client_is_muted(int room_id, int sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    bool muted = false;
    time_t now = time(NULL);
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (room.clients[i].sock == sock) { muted = room.clients[i].mute_until > now; break; }
    }
    pthread_mutex_unlock(&room.lock);
    return muted;
}

// 현재 방장 이름을 clnt_sock에게만 조용히 알려준다(입장/방 전환 직후용).
// 방장이 실제로 "바뀔" 때 알리는 승급 알림은 브라우저 쪽에서, 이전에 알던
// 방장 이름과 다를 때만 띄우도록 처리한다 - 그래야 이 최초 안내가 승급
// 알림으로 잘못 뜨지 않는다.
static void send_owner_info(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    std::string room_name = room.name;
    std::string owner_name = room.clnt_id > 0 ? std::string(room.clients[0].name) : "";
    pthread_mutex_unlock(&room.lock);
    if (!owner_name.empty())
        write_line(clnt_sock, "OWNER|" + room_name + "|" + owner_name);
}

// 이미 뮤트 중인 상태로 방에 들어오거나(방 전환) 재접속했다면, 새로 뜬
// 화면에도 남은 시간을 다시 알려줘서 카운트다운 UI(입력창 잠금)를 복구시킨다.
static void send_mute_status_if_active(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    std::string room_name = room.name;
    std::string name;
    int remaining = 0;
    time_t now = time(NULL);
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (room.clients[i].sock == clnt_sock && room.clients[i].mute_until > now)
        {
            name = room.clients[i].name;
            remaining = (int)(room.clients[i].mute_until - now);
            break;
        }
    }
    pthread_mutex_unlock(&room.lock);
    if (!name.empty())
        write_line(clnt_sock, "MUTE|" + room_name + "|" + name + "|" + std::to_string(remaining));
}

// 현재 커스텀 닉네임 색상이 설정된 사람들(나 자신 포함)을 clnt_sock에게만
// 조용히 알려준다 - 입장/방 전환 직후, 그 화면에도 이미 정해진 색으로 이름이
// 보이도록 하기 위함. "방금 바뀌었다"는 알림(COLOR 방송)과는 별개의 메시지다.
static void send_colors(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    std::string room_name = room.name;
    std::string msg = "COLORS|" + room_name;
    bool any = false;
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (!room.clients[i].color.empty())
        {
            msg += "|" + std::string(room.clients[i].name) + "|" + room.clients[i].color;
            any = true;
        }
    }
    pthread_mutex_unlock(&room.lock);
    if (any)
        write_line(clnt_sock, msg);
}

// 방을 오래 켜둔 채로 이야기가 아주 많이 쌓여도 다시 열어볼 때마다 전부 다시
// 보내지 않도록, 최근 이만큼만 재전송한다.
#define HISTORY_REPLAY_CAP 300

// 방을 새로 열어보는(처음 입장 또는 백그라운드에서 다시 활성화) 사람에게
// 최근 대화 내용을 그대로 재현해서 보여준다 - 안 그러면 "잠시 방을 꺼둔 사이"
// 온 메시지를 놓치게 된다. HISTORY_BEGIN/HISTMSG.../HISTORY_END 묶음으로,
// clnt_sock에게만 조용히 보낸다.
static void send_history(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    std::string room_name = room.name;
    size_t total = room.messages.size();
    size_t start = total > HISTORY_REPLAY_CAP ? total - HISTORY_REPLAY_CAP : 0;
    std::vector<MessageRecord> snapshot(room.messages.begin() + start, room.messages.end());
    pthread_mutex_unlock(&room.lock);

    if (snapshot.empty()) return;

    write_line(clnt_sock, "HISTORY_BEGIN|" + room_name);
    for (auto& m : snapshot)
    {
        // text가 마지막 필드라 "|"를 포함해도 안전하다. sender/orig_sender/
        // orig_preview/file_id는 기록될 때 이미 sanitize를 거쳤으므로 다시
        // 정리할 필요가 없다.
        write_line(clnt_sock, "HISTMSG|" + room_name + "|" + std::to_string(m.id) + "|" + m.sender + "|" +
            (m.deleted ? "1" : "0") + "|" + (m.edited ? "1" : "0") + "|" + (m.is_reply ? "1" : "0") + "|" +
            m.orig_sender + "|" + m.orig_preview + "|" +
            (m.is_file ? "1" : "0") + "|" + m.file_id + "|" + std::to_string(m.file_size) + "|" + m.text);
    }
    write_line(clnt_sock, "HISTORY_END|" + room_name);
}

// room_id 방에서 msg_id에 해당하는(삭제되지 않은) 메시지의 스냅샷을 얻는다.
// PIN 처리와 고정 상태 재전송(send_pin_state) 둘 다 이걸 쓴다.
static bool find_message_snapshot(int room_id, int msg_id, MessageRecord& out)
{
    Room& room = rooms[room_id];
    bool found = false;
    pthread_mutex_lock(&room.lock);
    for (auto& m : room.messages)
    {
        if (m.id == msg_id && !m.deleted) { out = m; found = true; break; }
    }
    pthread_mutex_unlock(&room.lock);
    return found;
}

// text가 마지막 필드라 "|"를 포함해도 안전하다. sender/file_id는 기록될 때
// 이미 sanitize를 거쳤으므로 다시 정리할 필요가 없다.
static std::string build_pinned_line(const std::string& room_name, const MessageRecord& m)
{
    return "PINNED|" + room_name + "|" + std::to_string(m.id) + "|" + m.sender + "|" +
        (m.is_file ? "1" : "0") + "|" + m.file_id + "|" + std::to_string(m.file_size) + "|" + m.text;
}

// m.reactions(사용자당 이모티콘 하나씩)를 "이모티콘|사용자" 쌍이 반복되는
// 형태로 그대로 나열한다(COLORS/ROSTER처럼 필드 개수가 가변이라 클라이언트도
// splitProtocol의 고정 maxParts가 아니라 "|" 전체 split 후 두 개씩 묶어서
// 읽는다). 반응이 하나도 없으면 마지막 필드 없이 "REACTIONS|방이름|msgId"만
// 보내며, 클라이언트는 이걸 "뱃지를 전부 지워라"로 해석한다. 사용자 이름은
// sanitize_username을 거쳐 "|"를 포함할 수 없으므로 구분자로 안전하다.
static std::string build_reactions_line(const std::string& room_name, const MessageRecord& m)
{
    std::string line = "REACTIONS|" + room_name + "|" + std::to_string(m.id);
    for (auto& r : m.reactions)
        line += "|" + r.second + "|" + r.first;
    return line;
}

// 방을 새로 열어보는 사람에게 지금 고정된 메시지가 있는지(있다면 그 내용을,
// 없다면 UNPINNED를) 알려준다 - ROSTER/OWNER처럼 활성화할 때마다 다시
// 동기화하는 조용한 상태 중 하나다.
static void send_pin_state(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    std::string room_name = room.name;
    int pinned_id = room.pinned_msg_id;
    pthread_mutex_unlock(&room.lock);

    MessageRecord m;
    if (pinned_id != -1 && find_message_snapshot(room_id, pinned_id, m))
        write_line(clnt_sock, build_pinned_line(room_name, m));
    else
        write_line(clnt_sock, "UNPINNED|" + room_name);
}

// 방을 새로 열어보는 사람에게 지금까지 쌓인 반응을 전부 재현해준다 -
// PINNED/COLORS와 같은 성격의 "활성화할 때마다 다시 동기화하는 조용한
// 상태"다. 삭제된 메시지는 건너뛴다(삭제되면 화면에서 반응도 같이 지워지므로
// 다시 보여줄 필요가 없다).
static void send_reactions_state(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    std::string room_name = room.name;
    std::vector<std::string> lines;
    for (auto& m : room.messages)
    {
        if (!m.deleted && !m.reactions.empty())
            lines.push_back(build_reactions_line(room_name, m));
    }
    pthread_mutex_unlock(&room.lock);

    for (auto& line : lines)
        write_line(clnt_sock, line);
}

// 이 방에서 새로 보낸 메시지(MSG/REPLY)에 고유 id를 배정하고 전체 내용을
// 기록한다 - EDIT/DELETE 권한 확인뿐 아니라, 나중에 이 방을 다시 열어보는
// 사람에게 HISTORY로 재전송하는 데도 쓴다. is_reply가 아니면
// orig_sender/orig_preview는 그냥 빈 문자열로 둔다.
static int record_new_message(int room_id, const std::string& sender, const std::string& text,
                               bool is_reply, const std::string& orig_sender, const std::string& orig_preview)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    int id = room.next_msg_id++;
    room.messages.push_back(MessageRecord{ id, sender, false, false, is_reply, text, orig_sender, orig_preview,
                                            false, "", 0 });
    pthread_mutex_unlock(&room.lock);
    return id;
}

// 업로드된 파일 하나를 새 메시지로 기록한다. 실제 파일 바이트는 이 서버가
// 아니라 chat_client가 디스크에 들고 있고, 여기는 메타데이터(파일명/용량/
// chat_client가 매긴 file_id)만 다른 메시지와 동일하게 취급한다 - 그래야
// 안읽음/미리보기/히스토리/삭제 같은 기존 인프라를 그대로 재사용할 수 있다.
static int record_new_file_message(int room_id, const std::string& sender, const std::string& file_id,
                                    const std::string& file_name, long file_size)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    int id = room.next_msg_id++;
    room.messages.push_back(MessageRecord{ id, sender, false, false, false, file_name, "", "",
                                            true, file_id, file_size });
    pthread_mutex_unlock(&room.lock);
    return id;
}

// msg_id가 sender 본인의, 아직 삭제되지 않은 메시지가 맞으면 새 내용으로
// 덮어쓰고 edited 표시를 남긴다. 성공 여부를 반환한다. 파일 메시지는 수정
// 대상이 아니다(is_file이면 애초에 매치시키지 않아 자연스럽게 "권한 없음"
// 에러가 나가도록 한다).
static bool apply_message_edit(int room_id, int msg_id, const std::string& sender, const std::string& new_text)
{
    Room& room = rooms[room_id];
    bool ok = false;
    pthread_mutex_lock(&room.lock);
    for (auto& m : room.messages)
    {
        if (m.id == msg_id && !m.deleted && !m.is_file && m.sender == sender)
        {
            m.text = new_text;
            m.edited = true;
            ok = true;
            break;
        }
    }
    pthread_mutex_unlock(&room.lock);
    return ok;
}

// msg_id가 sender 본인의, 아직 삭제되지 않은 메시지가 맞으면 삭제 표시를
// 남긴다. 성공 여부를 반환한다.
static bool apply_message_delete(int room_id, int msg_id, const std::string& sender)
{
    Room& room = rooms[room_id];
    bool ok = false;
    pthread_mutex_lock(&room.lock);
    for (auto& m : room.messages)
    {
        if (m.id == msg_id && !m.deleted && m.sender == sender)
        {
            m.deleted = true;
            ok = true;
            break;
        }
    }
    pthread_mutex_unlock(&room.lock);
    return ok;
}

// msg_id에 대한 username의 반응을 토글한다: 이미 같은 이모티콘으로 반응 중
// 이었으면 취소하고, 다른 이모티콘으로 반응 중이었으면 그걸로 교체하고,
// 반응이 없었으면 새로 추가한다(사용자당 메시지 하나에 이모티콘은 항상
// 하나만 허용 - 누가 반응했는지는 username으로만 찾으므로 메시지 소유자
// 권한은 따지지 않는다, 반응은 남의 메시지에도 누구나 붙일 수 있다).
// 대상 메시지를 찾았으면(삭제되지 않았다면) true.
static bool apply_message_reaction(int room_id, int msg_id, const std::string& username, const std::string& emoji)
{
    Room& room = rooms[room_id];
    bool ok = false;
    pthread_mutex_lock(&room.lock);
    for (auto& m : room.messages)
    {
        if (m.id == msg_id && !m.deleted)
        {
            ok = true;
            int existing = -1;
            for (size_t i = 0; i < m.reactions.size(); i++)
            {
                if (m.reactions[i].first == username) { existing = (int)i; break; }
            }
            if (existing != -1)
            {
                bool same_emoji = (m.reactions[existing].second == emoji);
                m.reactions.erase(m.reactions.begin() + existing);
                if (!same_emoji)
                    m.reactions.push_back({ username, emoji });
            }
            else
            {
                m.reactions.push_back({ username, emoji });
            }
            break;
        }
    }
    pthread_mutex_unlock(&room.lock);
    return ok;
}

// 문자열에서 프로토콜 구분자(|, 개행)를 제거하고 길이를 제한한다.
// 닉네임/방 이름/미리보기 모두 이 규칙을 따라야, "|"로 필드를 나누는 다른
// 모든 명령이 안전하게 동작한다.
static std::string sanitize_field(const std::string& raw, int max_len)
{
    std::string out;
    for (char c : raw)
    {
        if (c == '|' || c == '\n' || c == '\r') continue;
        out += c;
        if ((int)out.size() >= max_len - 1) break;
    }
    return out;
}

static std::string sanitize_username(const std::string& raw)
{
    std::string name = sanitize_field(raw, MAX_USERNAME_LEN);
    if (name.empty()) name = "guest";
    return name;
}

// 방 이름은 사용자 이름과 달리 비어있으면 그냥 에러로 취급한다("guest방"
// 같은 디폴트를 만들어주지 않는다) - 호출부에서 빈 문자열을 체크한다.
static std::string sanitize_room_name(const std::string& raw)
{
    return sanitize_field(raw, MAX_ROOM_NAME_LEN);
}

// 방 목록 문자열을 만든다("ROOMLIST|이름|인원수|비번유무|..."). include_private가
// false면 공개방만, name_filter가 비어있지 않으면 그 부분 문자열을 이름에
// 포함하는 방만 골라낸다(검색은 비공개방도 포함해서 보여준다).
// 아직 아무도 없는 방(clnt_id==0 - 막 예약되었거나 막 비워진 방)은 아직
// "존재하지 않는 방"처럼 취급해 목록에서 숨긴다.
static std::string build_room_list(bool include_private, const std::string& name_filter)
{
    struct Snapshot { int idx; std::string name; bool has_password; };
    std::vector<Snapshot> snap;

    pthread_mutex_lock(&rooms_lock);
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!rooms[i].in_use) continue;
        if (!include_private && !rooms[i].is_public) continue;
        if (!name_filter.empty() && rooms[i].name.find(name_filter) == std::string::npos) continue;
        snap.push_back({ i, rooms[i].name, !rooms[i].password.empty() });
    }
    pthread_mutex_unlock(&rooms_lock);

    std::string msg = "ROOMLIST";
    for (auto& s : snap)
    {
        pthread_mutex_lock(&rooms[s.idx].lock);
        int count = rooms[s.idx].clnt_id;
        pthread_mutex_unlock(&rooms[s.idx].lock);
        if (count <= 0) continue;
        msg += "|" + s.name + "|" + std::to_string(count) + "|" + (s.has_password ? "1" : "0");
    }
    return msg;
}

// 방이 비면(clnt_id==0) 이름 공간에서 제거해 그 이름을 재사용할 수 있게
// 한다. JOIN_ROOM/방 목록은 clnt_id==0인 방을 "존재하지 않는 방"처럼
// 취급하므로, in_use를 여기서 조금 늦게 꺼도(아주 짧은 경합 구간) 실제
// 오작동으로 이어지지 않는다.
static void destroy_room_if_empty(int room_id)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    bool empty = (room.clnt_id == 0);
    pthread_mutex_unlock(&room.lock);
    if (!empty) return;

    pthread_mutex_lock(&rooms_lock);
    room.in_use = false;
    room.name.clear();
    room.password.clear();
    pthread_mutex_unlock(&rooms_lock);
}

// 새 방을 예약한다(이름 중복 검사 + 빈 슬롯 찾기를 rooms_lock 하나로 원자적으로
// 처리). 성공하면 방 인덱스를, 실패하면 -1을 반환하고 err_out에 이유를 담는다.
// 예약된 방은 clnt_id==0이라 첫 멤버(생성자 본인)가 닉네임을 확정하기 전까지는
// 목록/검색/입장 어디에도 "존재하지 않는 방"으로 보인다.
static int create_room(const std::string& raw_name, bool is_public, const std::string& password, std::string& err_out)
{
    std::string name = sanitize_room_name(raw_name);
    if (name.empty())
    {
        err_out = "채팅방 이름을 입력해주세요.";
        return -1;
    }

    pthread_mutex_lock(&rooms_lock);

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].in_use && rooms[i].name == name)
        {
            pthread_mutex_unlock(&rooms_lock);
            err_out = "이미 존재하는 채팅방 이름입니다.";
            return -1;
        }
    }

    int slot = -1;
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (!rooms[i].in_use) { slot = i; break; }
    }
    if (slot == -1)
    {
        pthread_mutex_unlock(&rooms_lock);
        err_out = "채팅방을 더 만들 수 없습니다.";
        return -1;
    }

    rooms[slot].in_use = true;
    rooms[slot].name = name;
    rooms[slot].password = password;
    rooms[slot].is_public = is_public;
    // 이 슬롯을 예전에 쓰던 방의 메시지 기록이 새 방으로 새어 들어가지 않도록
    // 초기화한다(rooms_lock critical section 안에서의 쓰기는, 나중에 이 방을
    // 찾는 스레드가 같은 rooms_lock을 잡았다 풀 때 happens-before로 안전하게
    // 전달된다).
    rooms[slot].messages.clear();
    rooms[slot].next_msg_id = 1;
    rooms[slot].pinned_msg_id = -1;

    pthread_mutex_unlock(&rooms_lock);
    return slot;
}

// 이름으로 방을 찾아 비밀번호/인원 제한까지 검증한다. 성공하면 방 인덱스를,
// 실패하면 -1을 반환하고 err_out에 이유를 담는다.
static int find_joinable_room(const std::string& raw_name, const std::string& password, std::string& err_out)
{
    pthread_mutex_lock(&rooms_lock);
    int idx = -1;
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].in_use && rooms[i].name == raw_name) { idx = i; break; }
    }
    std::string room_password;
    if (idx != -1) room_password = rooms[idx].password;
    pthread_mutex_unlock(&rooms_lock);

    if (idx == -1)
    {
        err_out = "존재하지 않는 채팅방입니다.";
        return -1;
    }

    pthread_mutex_lock(&rooms[idx].lock);
    int count = rooms[idx].clnt_id;
    bool full = count >= MAX_CLIENT;
    pthread_mutex_unlock(&rooms[idx].lock);

    // 아직 아무도 없는 방(막 예약되었거나 막 비워진 방)은 목록과 마찬가지로
    // "존재하지 않는 방"으로 취급한다.
    if (count <= 0)
    {
        err_out = "존재하지 않는 채팅방입니다.";
        return -1;
    }
    if (full)
    {
        err_out = "채팅방 인원이 가득 찼습니다.";
        return -1;
    }
    if (!room_password.empty() && room_password != password)
    {
        err_out = "비밀번호가 틀렸습니다.";
        return -1;
    }

    return idx;
}

// 이름이 room_name인 in_use 방을 찾아 clnt_sock이 그 방의 멤버인지 확인하고,
// 맞으면 room_id/그 방에서 쓰는 닉네임을 알려준다. 스레드 로컬로 "내가 어느
// 방에 있는지" 캐시를 따로 두지 않고 항상 공유 상태에서 직접 찾는다 - 강퇴
// 등으로 다른 스레드가 이미 멤버십을 지웠어도 항상 최신 진실을 보게 된다.
static bool find_my_username_in_room(const std::string& room_name, int clnt_sock, int& room_id_out, std::string& username_out)
{
    pthread_mutex_lock(&rooms_lock);
    int idx = -1;
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].in_use && rooms[i].name == room_name) { idx = i; break; }
    }
    pthread_mutex_unlock(&rooms_lock);
    if (idx == -1) return false;

    Room& room = rooms[idx];
    pthread_mutex_lock(&room.lock);
    bool found = false;
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (room.clients[i].sock == clnt_sock) { username_out = room.clients[i].name; found = true; break; }
    }
    pthread_mutex_unlock(&room.lock);
    if (!found) return false;

    room_id_out = idx;
    return true;
}

// 지금 in_use인 방 id 목록의 스냅샷. "이 연결이 속해 있을 수도 있는 모든
// 방"을 훑어야 하는 작업(연결 종료 정리, 방 전환 시 비활성화 등)에 쓴다.
static std::vector<int> snapshot_in_use_room_ids()
{
    std::vector<int> ids;
    pthread_mutex_lock(&rooms_lock);
    for (int i = 0; i < MAX_ROOMS; i++)
        if (rooms[i].in_use) ids.push_back(i);
    pthread_mutex_unlock(&rooms_lock);
    return ids;
}

struct RemovalResult
{
    bool        found;
    std::string username;
    bool        owner_changed;
    std::string new_owner_name;
};

// room_id에서 clnt_sock 멤버를 제거한다(있다면). 방장 재위임 여부/새 방장
// 이름을 알려준다. 무엇을 방송할지(LEAVE vs KICK)는 호출부가 정한다.
static RemovalResult remove_from_room(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    RemovalResult r{ false, "", false, "" };
    pthread_mutex_lock(&room.lock);
    int idx = -1;
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (room.clients[i].sock == clnt_sock) { idx = i; break; }
    }
    if (idx != -1)
    {
        r.found = true;
        r.username = room.clients[idx].name;
        for (int j = idx; j < room.clnt_id - 1; j++)
            room.clients[j] = room.clients[j + 1];
        room.clnt_id--;
        // 나간 사람이 방장(index 0)이었고 방에 아직 남은 사람이 있다면,
        // 그다음으로 먼저 들어온 사람(이제 index 0)이 자동으로 방장이 된다.
        if (idx == 0 && room.clnt_id > 0)
        {
            r.owner_changed = true;
            r.new_owner_name = room.clients[0].name;
        }
    }
    pthread_mutex_unlock(&room.lock);
    return r;
}

// 자진 퇴장(LEAVE_ROOM)이나 진짜 접속 끊김으로 room_id를 나올 때 공통으로
// 쓴다: 제거 + "나갔습니다" 알림 + 방장 재위임 알림 + 빈 방 정리.
static void leave_room_and_notify(int room_id, int clnt_sock)
{
    std::string room_name;
    { Room& r0 = rooms[room_id]; pthread_mutex_lock(&r0.lock); room_name = r0.name; pthread_mutex_unlock(&r0.lock); }

    RemovalResult r = remove_from_room(room_id, clnt_sock);
    if (!r.found) return;

    notify_room(room_id, -1, "LEAVE|" + room_name + "|" + r.username, r.username + "님이 나갔습니다.", false);
    if (r.owner_changed)
        notify_room(room_id, -1, "OWNER|" + room_name + "|" + r.new_owner_name, r.new_owner_name + "님이 방장으로 승급했습니다.", false);

    destroy_room_if_empty(room_id);
}

// clnt_sock 연결이 완전히 끊겼을 때, 이 연결이 멤버로 남아있는 모든 방에서
// 정리한다. leave_room_and_notify는 그 방의 멤버가 아니면 조용히 아무 일도
// 안 하므로, 관계없는 방까지 그냥 다 훑어도 안전하다.
static void cleanup_all_memberships(int clnt_sock)
{
    for (int room_id : snapshot_in_use_room_ids())
        leave_room_and_notify(room_id, clnt_sock);
}

// 방 전환/로비 복귀 시, 이 연결이 활성으로 걸어뒀을 수 있는 모든 멤버십을
// 비활성화한다(클라이언트는 항상 최대 하나만 활성화한다고 가정하지만, 방어적으로
// 전체를 훑는다 - 방 개수가 적어 비용도 저렴하다).
static void deactivate_all_memberships(int clnt_sock)
{
    for (int room_id : snapshot_in_use_room_ids())
    {
        Room& room = rooms[room_id];
        pthread_mutex_lock(&room.lock);
        for (int i = 0; i < room.clnt_id; i++)
        {
            if (room.clients[i].sock == clnt_sock) { room.clients[i].is_active = false; break; }
        }
        pthread_mutex_unlock(&room.lock);
    }
}

// room_id에서 clnt_sock 멤버를 활성 상태로 바꾸고 안읽음 카운트를 0으로
// 리셋한다("읽음" 처리) - 방을 새로 열어보는 순간에 쓴다.
static void activate_membership(int room_id, int clnt_sock)
{
    Room& room = rooms[room_id];
    pthread_mutex_lock(&room.lock);
    for (int i = 0; i < room.clnt_id; i++)
    {
        if (room.clients[i].sock == clnt_sock)
        {
            room.clients[i].is_active = true;
            room.clients[i].unread_count = 0;
            break;
        }
    }
    pthread_mutex_unlock(&room.lock);
}

void* handle_clnt(void* arg)
{
    int clnt_sock = *((int*)arg);
    delete (int*)arg;

    std::string carry;
    std::string line;
    int pending_room_id = -1; // CREATE/JOIN_ROOM 성공 후 닉네임 확정을 기다리는 방(-1이면 없음)

    while (read_line(clnt_sock, carry, line))
    {
        std::string parts[2];
        split_line(line, '|', 2, parts);
        const std::string& cmd = parts[0];

        if (cmd == "HELLO")
        {
            // 로비 화면을 띄우라는 뜻. 이 연결이 이미 참여 중인 방이 있어도
            // 그대로 로비부터 보여준다(어느 방을 보고 있었는지는 브라우저
            // 새로고침으로 사라지는 화면 상태일 뿐이다) - 클라이언트가 곧이어
            // MYROOMS로 참여 중인 방 목록을 다시 받아간다.
            write_line(clnt_sock, "LOBBY");
        }
        else if (cmd == "ROOMS")
        {
            write_line(clnt_sock, build_room_list(false, ""));
        }
        else if (cmd == "SEARCH")
        {
            write_line(clnt_sock, build_room_list(true, parts[1]));
        }
        else if (cmd == "MYROOMS")
        {
            // 이 연결이 지금 멤버로 있는 모든 방을 훑어 "방이름|인원수|안읽음|미리보기"를 만든다.
            std::string msg = "MYROOMS";
            for (int room_id : snapshot_in_use_room_ids())
            {
                Room& room = rooms[room_id];
                pthread_mutex_lock(&room.lock);
                for (int i = 0; i < room.clnt_id; i++)
                {
                    if (room.clients[i].sock == clnt_sock)
                    {
                        msg += "|" + room.name + "|" + std::to_string(room.clnt_id) + "|" +
                               std::to_string(room.clients[i].unread_count) + "|" + room.clients[i].last_preview;
                        break;
                    }
                }
                pthread_mutex_unlock(&room.lock);
            }
            write_line(clnt_sock, msg);
        }
        else if (cmd == "CREATE")
        {
            // parts[1] = "방이름|공개여부(0/1)|비밀번호" 형태. 비밀번호가
            // 마지막 필드라 "|"를 포함해도 안전하다.
            std::string sub[3];
            split_line(parts[1], '|', 3, sub);
            bool is_public = (sub[1] == "1");
            std::string err;
            int slot = create_room(sub[0], is_public, sub[2], err);
            if (slot == -1)
                write_line(clnt_sock, "ERR|" + err);
            else
            {
                write_line(clnt_sock, "ROOM_OK|" + rooms[slot].name);
                pending_room_id = slot;
            }
        }
        else if (cmd == "JOIN_ROOM")
        {
            // parts[1] = "방이름|비밀번호" 형태.
            std::string sub[2];
            split_line(parts[1], '|', 2, sub);
            std::string err;
            int idx = find_joinable_room(sub[0], sub[1], err);
            if (idx == -1)
                write_line(clnt_sock, "ERR|" + err);
            else
            {
                write_line(clnt_sock, "ROOM_OK|" + rooms[idx].name);
                pending_room_id = idx;
            }
        }
        else if (cmd == "CANCEL")
        {
            // "뒤로가기"(닉네임 입력 단계에서): 방금 만든 방이었다면(아직
            // 아무도 없으므로) 정리하고 로비로 돌아간다.
            if (pending_room_id != -1)
            {
                destroy_room_if_empty(pending_room_id);
                pending_room_id = -1;
            }
            write_line(clnt_sock, "LOBBY");
        }
        else if (cmd == "JOIN")
        {
            // 닉네임 확정: 직전에 CREATE/JOIN_ROOM으로 예약해둔 pending_room_id에 등록한다.
            if (pending_room_id == -1 || parts[1].empty())
            {
                write_line(clnt_sock, "ERR|닉네임을 입력해주세요.");
            }
            else
            {
                std::string candidate = sanitize_username(parts[1]);
                Room& room = rooms[pending_room_id];

                // 이 방을 새로 열어보는 순간이니, 이전에 다른 방을 활성으로
                // 걸어뒀다면(이미 다른 방에 들어가 있는 채로 새 방을 또
                // 만들거나 들어온 경우) 그 방은 이제 백그라운드로 물러난다.
                // 아직 등록 전이라 이 방 자체는 여기서 안 건드려진다.
                deactivate_all_memberships(clnt_sock);

                pthread_mutex_lock(&room.lock);
                bool taken = false;
                for (int i = 0; i < room.clnt_id; i++)
                {
                    if (candidate == room.clients[i].name) { taken = true; break; }
                }

                if (taken)
                {
                    pthread_mutex_unlock(&room.lock);
                    write_line(clnt_sock, "ERR|이미 사용 중인 닉네임입니다.");
                }
                else if (room.clnt_id >= MAX_CLIENT)
                {
                    pthread_mutex_unlock(&room.lock);
                    write_line(clnt_sock, "ERR|채팅방 인원이 가득 찼습니다.");
                }
                else
                {
                    room.clients[room.clnt_id].sock = clnt_sock;
                    strncpy(room.clients[room.clnt_id].name, candidate.c_str(), MAX_USERNAME_LEN - 1);
                    room.clients[room.clnt_id].name[MAX_USERNAME_LEN - 1] = '\0';
                    room.clients[room.clnt_id].mute_until = 0;
                    room.clients[room.clnt_id].color.clear();
                    room.clients[room.clnt_id].is_active = true; // 방금 들어왔으니 지금 보고 있는 중
                    room.clients[room.clnt_id].unread_count = 0;
                    room.clients[room.clnt_id].last_preview.clear();
                    room.clnt_id++;
                    std::string room_name = room.name;
                    pthread_mutex_unlock(&room.lock);

                    int joined_room_id = pending_room_id;
                    pending_room_id = -1;

                    write_line(clnt_sock, "JOINED|" + room_name + "|" + candidate);
                    // 이 방에 이미 쌓여있던 대화가 있다면(다른 사람들끼리
                    // 나눈 얘기든, 예전에 있다가 나간 적 있는 방이든) 먼저
                    // 재현해서 보여준다.
                    send_history(joined_room_id, clnt_sock);
                    // 새로 들어온 사람은 자신보다 먼저 들어와 있던 사람들의 존재를
                    // 알 방법이 없다(그들의 JOIN 방송은 이미 지나갔으므로). 귓속말
                    // 대상 목록 등에 쓸 수 있도록, 이미 접속해 있는 사람들의 이름을
                    // 나에게만 조용히 알려준다.
                    notify_room(joined_room_id, clnt_sock, "JOIN|" + room_name + "|" + candidate, candidate + "님이 입장했습니다.", false);
                    send_roster(joined_room_id, clnt_sock);
                    send_owner_info(joined_room_id, clnt_sock);
                    send_colors(joined_room_id, clnt_sock);
                    send_pin_state(joined_room_id, clnt_sock);
                    send_reactions_state(joined_room_id, clnt_sock);
                }
            }
        }
        else if (cmd == "SWITCH_ROOM")
        {
            // parts[1] = 방이름("" = 활성화 없이 비활성화만 - 로비로 돌아갈 때).
            const std::string& room_name = parts[1];
            deactivate_all_memberships(clnt_sock);

            if (room_name.empty())
            {
                write_line(clnt_sock, "ACTIVE|");
            }
            else
            {
                int room_id;
                std::string username;
                if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
                {
                    write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
                }
                else
                {
                    activate_membership(room_id, clnt_sock);
                    // 방마다 닉네임이 다를 수 있으므로(같은 연결이라도 방마다
                    // 독립적으로 정한 닉네임), 지금 이 방에서 쓰는 닉네임을
                    // 같이 알려준다 - 클라이언트가 자기 username을 갱신해야
                    // 이후 채팅/수정/삭제 등을 올바른 이름으로 요청할 수 있다.
                    write_line(clnt_sock, "ACTIVE|" + room_name + "|" + username);
                    // 백그라운드로 밀려있는 동안 놓친 대화를 다시 보여준다.
                    send_history(room_id, clnt_sock);
                    // 백그라운드 상태에서는 방장/뮤트/색상 변경을 놓쳤을 수 있으므로,
                    // 방을 다시 열어볼 때마다 최신 상태로 다시 동기화한다.
                    send_roster(room_id, clnt_sock);
                    send_owner_info(room_id, clnt_sock);
                    send_mute_status_if_active(room_id, clnt_sock);
                    send_colors(room_id, clnt_sock);
                    send_pin_state(room_id, clnt_sock);
                    send_reactions_state(room_id, clnt_sock);
                }
            }
        }
        else if (cmd == "MARK_READ")
        {
            // "내 채팅방" 목록에서 방을 열지 않고도 우클릭 메뉴로 안읽음만 지운다.
            const std::string& room_name = parts[1];
            int room_id;
            std::string username;
            if (find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                Room& room = rooms[room_id];
                pthread_mutex_lock(&room.lock);
                int member_count = room.clnt_id;
                std::string preview;
                for (int i = 0; i < room.clnt_id; i++)
                {
                    if (room.clients[i].sock == clnt_sock)
                    {
                        room.clients[i].unread_count = 0;
                        preview = room.clients[i].last_preview;
                        break;
                    }
                }
                pthread_mutex_unlock(&room.lock);
                // 안읽음만 0으로 지우는 것뿐이라 알람 팝업은 필요 없다(isMsg=0, msg_id=-1).
                write_line(clnt_sock, "ROOMUPDATE|" + room_name + "|0|" + std::to_string(member_count) + "|0|-1|" + preview);
            }
        }
        else if (cmd == "MSG")
        {
            // parts[1] = "방이름|내용"
            std::string sub[2];
            split_line(parts[1], '|', 2, sub);
            const std::string& room_name = sub[0];
            const std::string& text = sub[1];

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (client_is_muted(room_id, clnt_sock))
            {
                write_line(clnt_sock, "ERR|채팅이 제한되어 있습니다.");
            }
            else
            {
                // 수정/삭제(EDIT/DELETE)가 이 id로 메시지를 특정하므로, 보낸 사람
                // 본인도 포함해서 전원에게 알린다(활성 상태인 본인은 그대로
                // 전체 방송을 받는다) - 그래야 본인 메시지의 서버 id를 알고
                // 나중에 수정/삭제를 요청할 수 있다.
                int msg_id = record_new_message(room_id, username, text, false, "", "");
                notify_room(room_id, -1,
                    "MSG|" + room_name + "|" + std::to_string(msg_id) + "|" + username + "|" + text,
                    username + ": " + truncate_preview(text), true, msg_id);
            }
        }
        else if (cmd == "REPLY")
        {
            // parts[1] = "방이름|원본작성자|원본미리보기|답장내용"
            std::string sub[4];
            split_line(parts[1], '|', 4, sub);
            const std::string& room_name = sub[0];

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (client_is_muted(room_id, clnt_sock))
            {
                write_line(clnt_sock, "ERR|채팅이 제한되어 있습니다.");
            }
            else
            {
                // 원본작성자/미리보기는 "|"로 필드가 갈리는 위치에 있으므로
                // (마지막 필드가 아님) sanitize로 구분자를 제거해둔다 - 답장
                // 내용은 마지막 필드라 그대로 둬도 안전하다.
                std::string orig_sender = sanitize_field(sub[1], MAX_USERNAME_LEN);
                std::string orig_preview = sanitize_field(sub[2], 200);
                const std::string& body = sub[3];

                int msg_id = record_new_message(room_id, username, body, true, orig_sender, orig_preview);
                notify_room(room_id, -1,
                    "REPLY|" + room_name + "|" + std::to_string(msg_id) + "|" + username + "|" +
                        orig_sender + "|" + orig_preview + "|" + body,
                    username + ": " + truncate_preview(body), true, msg_id);
            }
        }
        else if (cmd == "FILE")
        {
            // parts[1] = "방이름|fileId|파일용량(바이트)|원본 파일명". 이 명령은
            // 브라우저가 직접 보내는 게 아니라, chat_client가 HTTP로 파일을 받아
            // 디스크에 저장한 뒤(실제 바이트는 이 서버가 아예 안 본다) 같은 TCP
            // 연결로 대신 보내주는 메타데이터 알림이다.
            std::string sub[4];
            split_line(parts[1], '|', 4, sub);
            const std::string& room_name = sub[0];
            std::string file_id = sanitize_field(sub[1], 64);
            long file_size = atol(sub[2].c_str());
            std::string file_name = sanitize_field(sub[3], 200);

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (client_is_muted(room_id, clnt_sock))
            {
                write_line(clnt_sock, "ERR|채팅이 제한되어 있습니다.");
            }
            else if (file_id.empty() || file_name.empty())
            {
                write_line(clnt_sock, "ERR|잘못된 파일 정보입니다.");
            }
            else
            {
                int msg_id = record_new_file_message(room_id, username, file_id, file_name, file_size);
                notify_room(room_id, -1,
                    "FILE|" + room_name + "|" + std::to_string(msg_id) + "|" + username + "|" +
                        file_id + "|" + std::to_string(file_size) + "|" + file_name,
                    username + ": [파일] " + file_name, true, msg_id);
            }
        }
        else if (cmd == "EDIT")
        {
            // parts[1] = "방이름|msgId|새 내용"
            std::string sub[3];
            split_line(parts[1], '|', 3, sub);
            const std::string& room_name = sub[0];
            int msg_id = atoi(sub[1].c_str());
            const std::string& new_text = sub[2];

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (client_is_muted(room_id, clnt_sock))
            {
                // 수정은 실질적으로 새 내용을 내보내는 것과 같으므로, 뮤트가
                // "옛날 메시지를 수정해서 새 내용을 우회 전송"하는 구멍이
                // 되지 않도록 똑같이 막는다.
                write_line(clnt_sock, "ERR|채팅이 제한되어 있습니다.");
            }
            else if (!apply_message_edit(room_id, msg_id, username, new_text))
            {
                write_line(clnt_sock, "ERR|메시지를 수정할 권한이 없습니다.");
            }
            else
            {
                // 이미 보낸 메시지에 대한 정정이라 백그라운드 인원에게 unread를
                // 새로 늘리지 않고, 지금 보고 있는 사람에게만 그대로 알린다.
                broadcast_line(room_id, -1, "EDITED|" + room_name + "|" + std::to_string(msg_id) + "|" + new_text);
            }
        }
        else if (cmd == "DELETE")
        {
            // parts[1] = "방이름|msgId"
            std::string sub[2];
            split_line(parts[1], '|', 2, sub);
            const std::string& room_name = sub[0];
            int msg_id = atoi(sub[1].c_str());

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (!apply_message_delete(room_id, msg_id, username))
            {
                write_line(clnt_sock, "ERR|메시지를 삭제할 권한이 없습니다.");
            }
            else
            {
                broadcast_line(room_id, -1, "DELETED|" + room_name + "|" + std::to_string(msg_id));

                // 방금 지운 메시지가 상단에 고정돼 있었다면, 이미 삭제된 내용을
                // 팝업에 계속 보여줄 수 없으므로 자동으로 고정을 해제한다.
                Room& room = rooms[room_id];
                bool was_pinned;
                pthread_mutex_lock(&room.lock);
                was_pinned = (room.pinned_msg_id == msg_id);
                if (was_pinned) room.pinned_msg_id = -1;
                pthread_mutex_unlock(&room.lock);
                if (was_pinned)
                    broadcast_line(room_id, -1, "UNPINNED|" + room_name);
            }
        }
        else if (cmd == "PIN")
        {
            // parts[1] = "방이름|msgId". 방장만 가능.
            std::string sub[2];
            split_line(parts[1], '|', 2, sub);
            const std::string& room_name = sub[0];
            int msg_id = atoi(sub[1].c_str());

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (!client_is_owner(room_id, clnt_sock))
            {
                write_line(clnt_sock, "ERR|방장만 사용할 수 있는 명령어입니다.");
            }
            else
            {
                MessageRecord m;
                if (!find_message_snapshot(room_id, msg_id, m))
                {
                    write_line(clnt_sock, "ERR|고정할 메시지를 찾을 수 없습니다.");
                }
                else
                {
                    Room& room = rooms[room_id];
                    pthread_mutex_lock(&room.lock);
                    room.pinned_msg_id = msg_id;
                    pthread_mutex_unlock(&room.lock);
                    // 다른 메시지가 이미 고정돼 있었더라도 그냥 새 걸로 덮어쓴다
                    // (한 방에 고정 메시지는 항상 최대 하나).
                    broadcast_line(room_id, -1, build_pinned_line(room_name, m));
                }
            }
        }
        else if (cmd == "UNPIN")
        {
            // parts[1] = "방이름". 방장만 가능.
            const std::string& room_name = parts[1];

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (!client_is_owner(room_id, clnt_sock))
            {
                write_line(clnt_sock, "ERR|방장만 사용할 수 있는 명령어입니다.");
            }
            else
            {
                Room& room = rooms[room_id];
                pthread_mutex_lock(&room.lock);
                room.pinned_msg_id = -1;
                pthread_mutex_unlock(&room.lock);
                broadcast_line(room_id, -1, "UNPINNED|" + room_name);
            }
        }
        else if (cmd == "REACT")
        {
            // parts[1] = "방이름|msgId|이모티콘". 방장/작성자 구분 없이 누구나
            // 가능하다.
            std::string sub[3];
            split_line(parts[1], '|', 3, sub);
            const std::string& room_name = sub[0];
            int msg_id = atoi(sub[1].c_str());
            const std::string& emoji = sub[2];

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (!is_valid_reaction_emoji(emoji))
            {
                write_line(clnt_sock, "ERR|지원하지 않는 이모티콘입니다.");
            }
            else if (!apply_message_reaction(room_id, msg_id, username, emoji))
            {
                write_line(clnt_sock, "ERR|반응할 메시지를 찾을 수 없습니다.");
            }
            else
            {
                // EDIT/DELETE/PIN과 마찬가지로 "이미 보낸 메시지에 대한 부가
                // 정보"라서 notify_room이 아니라 broadcast_line을 쓴다 - 백그라운드
                // 인원의 안읽음 카운트를 늘리거나 알림 팝업을 띄우지 않는다.
                MessageRecord m;
                find_message_snapshot(room_id, msg_id, m);
                broadcast_line(room_id, -1, build_reactions_line(room_name, m));
            }
        }
        else if (cmd == "WHISPER")
        {
            // parts[1] = "방이름|대상|귓속말 내용"
            std::string sub[3];
            split_line(parts[1], '|', 3, sub);
            const std::string& room_name = sub[0];
            const std::string& target = sub[1];
            const std::string& whisper_text = sub[2];

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (client_is_muted(room_id, clnt_sock))
            {
                write_line(clnt_sock, "ERR|채팅이 제한되어 있습니다.");
            }
            else
            {
                Room& room = rooms[room_id];
                int target_sock = -1;
                pthread_mutex_lock(&room.lock);
                for (int i = 0; i < room.clnt_id; i++)
                {
                    if (target == room.clients[i].name) { target_sock = room.clients[i].sock; break; }
                }
                pthread_mutex_unlock(&room.lock);

                if (target_sock != -1)
                    // 귓속말은 단순화를 위해 활성/백그라운드 구분 없이 항상 바로
                    // 전달한다 - 원래 이 앱이 메시지 기록을 전혀 남기지 않는
                    // 휘발성 설계이므로, 백그라운드 중에 받으면 그냥 놓치는 것도
                    // 일관된 동작으로 본다.
                    write_line(target_sock, "WHISPER|" + room_name + "|" + username + "|" + target + "|" + whisper_text);
                else
                    write_line(clnt_sock, "ERR|해당 명령어를 찾을 수 없습니다.");
            }
        }
        else if (cmd == "KICK")
        {
            // parts[1] = "방이름|대상"
            std::string sub[2];
            split_line(parts[1], '|', 2, sub);
            const std::string& room_name = sub[0];
            const std::string& target = sub[1];

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (!client_is_owner(room_id, clnt_sock))
            {
                write_line(clnt_sock, "ERR|방장만 사용할 수 있는 명령어입니다.");
            }
            else if (target == username)
            {
                write_line(clnt_sock, "ERR|자기 자신은 추방할 수 없습니다.");
            }
            else
            {
                Room& room = rooms[room_id];
                int target_sock = -1;
                pthread_mutex_lock(&room.lock);
                for (int i = 0; i < room.clnt_id; i++)
                {
                    if (target == room.clients[i].name) { target_sock = room.clients[i].sock; break; }
                }
                pthread_mutex_unlock(&room.lock);

                if (target_sock == -1)
                {
                    write_line(clnt_sock, "ERR|해당 유저를 찾을 수 없습니다.");
                }
                else
                {
                    // 이 방 하나에서만 제거한다 - target이 다른 방에도 들어가
                    // 있을 수 있으므로 연결 자체는 절대 건드리지 않는다(예전엔
                    // shutdown()으로 연결 전체를 끊었는데, 여러 방에 동시에
                    // 있을 수 있게 되면서 그러면 안 된다).
                    RemovalResult r = remove_from_room(room_id, target_sock);
                    if (r.found)
                    {
                        notify_room(room_id, target_sock, "KICK|" + room_name + "|" + r.username,
                            r.username + "님이 방장에 의해 추방됐습니다.", false);
                        write_line(target_sock, "KICKED|" + room_name);
                        if (r.owner_changed)
                            notify_room(room_id, -1, "OWNER|" + room_name + "|" + r.new_owner_name,
                                r.new_owner_name + "님이 방장으로 승급했습니다.", false);
                        destroy_room_if_empty(room_id);
                    }
                }
            }
        }
        else if (cmd == "MUTE")
        {
            // parts[1] = "방이름|대상|초"
            std::string sub[3];
            split_line(parts[1], '|', 3, sub);
            const std::string& room_name = sub[0];
            const std::string& target = sub[1];
            int seconds = atoi(sub[2].c_str());

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (!client_is_owner(room_id, clnt_sock))
            {
                write_line(clnt_sock, "ERR|방장만 사용할 수 있는 명령어입니다.");
            }
            else if (seconds <= 0)
            {
                write_line(clnt_sock, "ERR|제한 시간은 1초 이상이어야 합니다.");
            }
            else
            {
                Room& room = rooms[room_id];
                bool found = false;
                pthread_mutex_lock(&room.lock);
                for (int i = 0; i < room.clnt_id; i++)
                {
                    if (target == room.clients[i].name)
                    {
                        room.clients[i].mute_until = time(NULL) + seconds;
                        found = true;
                        break;
                    }
                }
                pthread_mutex_unlock(&room.lock);

                if (!found)
                    write_line(clnt_sock, "ERR|해당 유저를 찾을 수 없습니다.");
                else
                    notify_room(room_id, -1, "MUTE|" + room_name + "|" + target + "|" + std::to_string(seconds),
                        target + "님의 채팅이 " + std::to_string(seconds) + "초 동안 제한되었습니다.", false);
            }
        }
        else if (cmd == "COLOR")
        {
            // parts[1] = "방이름|색상"
            std::string sub[2];
            split_line(parts[1], '|', 2, sub);
            const std::string& room_name = sub[0];
            const std::string& color_name = sub[1];

            int room_id;
            std::string username;
            if (!find_my_username_in_room(room_name, clnt_sock, room_id, username))
            {
                write_line(clnt_sock, "ERR|참여 중인 방이 아닙니다.");
            }
            else if (!is_valid_color(color_name))
            {
                write_line(clnt_sock, "ERR|올바르지 않은 색상입니다.");
            }
            else
            {
                Room& room = rooms[room_id];
                pthread_mutex_lock(&room.lock);
                for (int i = 0; i < room.clnt_id; i++)
                {
                    if (room.clients[i].sock == clnt_sock) { room.clients[i].color = color_name; break; }
                }
                pthread_mutex_unlock(&room.lock);

                notify_room(room_id, -1, "COLOR|" + room_name + "|" + username + "|" + color_name,
                    username + "님의 닉네임 색상이 " + color_name + "로 변경되었습니다.", false);
            }
        }
        else if (cmd == "LEAVE_ROOM")
        {
            // parts[1] = 방이름. "나가기" 버튼: 이 방 하나에서만 정식으로
            // 탈퇴한다(다른 방에 든 멤버십은 그대로 유지) - 연결 자체는
            // 살아있어야 하므로 소켓은 절대 건드리지 않는다.
            const std::string& room_name = parts[1];
            int room_id;
            std::string username;
            if (find_my_username_in_room(room_name, clnt_sock, room_id, username))
                leave_room_and_notify(room_id, clnt_sock);
            // 이미 멤버가 아니면 조용히 무시한다.
        }
        // 그 외 알 수 없는 명령은 지금은 무시한다.
    }

    // 접속이 완전히 끊겼다: pending 상태로 남아있던 방(닉네임 확정 전)과,
    // 실제로 들어가 있던 모든 방에서 각각 정리한다.
    if (pending_room_id != -1)
        destroy_room_if_empty(pending_room_id);
    cleanup_all_memberships(clnt_sock);
    close(clnt_sock);
    return NULL;
}

int main(int argc, char* argv[])
{
    //관리는 번호가 편하다
    int serv_sock;
    int clnt_sock;
    pthread_t t_id;

    //소켓 열기 위해 필요한 정보가 담겨있는 인스턴스 (주소, 포트)
    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;
    unsigned int clnt_addr_size;

    if(argc != 2)   //"포트가 너무 많이 열려있으면 위험하다"
    {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // 방 배열 초기화. pthread_mutex_t는 배열 원소에 PTHREAD_MUTEX_INITIALIZER를
    // 바로 못 쓰므로 pthread_mutex_init으로 직접 초기화한다.
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        rooms[i].in_use = false;
        rooms[i].clnt_id = 0;
        rooms[i].next_msg_id = 1;
        rooms[i].pinned_msg_id = -1;
        pthread_mutex_init(&rooms[i].lock, NULL);
    }

    //socket()을 호출한 순간 전화기를 사는 동작이 이루어진다. 의미가 생긴다.
    //PF_INET : ipv4 쓰겠다 (protocol family)
    //SOCK_STREAM : TCP, SOCK_DGRAM : UDP
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);

    if(serv_sock == -1) //소켓이 제대로 동작하지 못했다.
    {
        error_handling("socket() error");
    }

    // 서버를 자주 재시작하며 테스트할 때 "Address already in use"를 피하기 위함.
    int optval = 1;
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&serv_addr, 0, sizeof(serv_addr));   //메모리 세팅. 모두 0으로.
    serv_addr.sin_family = AF_INET;             //address family. 주소체계. ipv4 쓰겠다
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);  //내 ip주소  host to network long : Little Endien에서 Big Endien으로 바꿈
    serv_addr.sin_port = htons(atoi(argv[1]));  // short

    //전화기랑 세팅된 유심 정보랑 묶는다
    if(bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
    {
        error_handling("bind() error");
    }

    //서버는 전화를 기다린다. 동시에 전화 걸 수 있는 사람의 개수가 5명을 넘으면 안 된다.
    if(listen(serv_sock, 5) == -1)
    {
        error_handling("listen() error");
    }

    clnt_addr_size = sizeof(clnt_addr);

    while(1)    //서버는 항시 가동 중이어야 한다.
    {
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);

        if(clnt_sock == -1)
        {
            perror("accept() error");
            continue;   // 접속 하나 실패했다고 서버 전체가 죽으면 안 된다.
        }

        // clnt_sock(int)을 그대로 void*로 캐스팅해서 넘기면, handle_clnt에서
        // 이를 포인터로 역참조할 때 정의되지 않은 동작(UB)이 된다.
        // 힙에 int를 하나 할당해서 넘기고, 스레드 쪽에서 읽은 뒤 바로 delete한다.
        int* pclnt_sock = new int(clnt_sock);
        if (pthread_create(&t_id, NULL, handle_clnt, pclnt_sock) != 0)
        {
            perror("pthread_create() error");
            delete pclnt_sock;
            close(clnt_sock);
            continue;
        }
        pthread_detach(t_id);   // join하지 않으므로 반드시 detach해서 자원 누수를 막는다.
    }

    return 0;
}
