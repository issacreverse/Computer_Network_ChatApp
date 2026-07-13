#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <string>
#include <unistd.h>

#define MAX_CLIENT       64
#define MAX_USERNAME_LEN 32
#define READ_CHUNK       512

// TCP는 스트림이라 한 번의 read()가 한 줄(logical line)과 정확히 일치한다는
// 보장이 없다. carry에 이전에 남은 조각을 이어붙여가며 '\n' 단위로 잘라낸다.
// 소켓이 닫히거나 오류가 나면 false를 반환한다.
inline bool read_line(int fd, std::string& carry, std::string& out_line)
{
    size_t nl;
    while ((nl = carry.find('\n')) == std::string::npos)
    {
        char buf[READ_CHUNK];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) return false;
        carry.append(buf, n);
    }
    out_line = carry.substr(0, nl);
    carry.erase(0, nl + 1);
    return true;
}

// line을 delim 기준으로 최대 max_parts개로 나눈다.
// 마지막 조각은 남은 문자열 전체(자유 텍스트, delim을 포함할 수 있음)를 담는다.
inline void split_line(const std::string& line, char delim, int max_parts, std::string out[])
{
    size_t start = 0;
    for (int i = 0; i < max_parts - 1; i++)
    {
        size_t pos = line.find(delim, start);
        if (pos == std::string::npos)
        {
            out[i] = line.substr(start);
            start = std::string::npos;
            break;
        }
        out[i] = line.substr(start, pos - start);
        start = pos + 1;
    }
    if (start != std::string::npos)
        out[max_parts - 1] = line.substr(start);
}

inline bool write_line(int fd, const std::string& line)
{
    std::string with_nl = line + "\n";
    ssize_t n = write(fd, with_nl.data(), with_nl.size());
    return n == (ssize_t)with_nl.size();
}

#endif
