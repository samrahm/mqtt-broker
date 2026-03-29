#pragma once
#include <iostream>
#include <stdio.h>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
using namespace std;

#undef FunctionName
#define FunctionName Reporter(__FUNCTION__, __FILE__, __LINE__)

enum class LEVEL
{
    INFO,
    DEBUG,
    VERBOSE,
    WARNING,
    ERROR
};

class Logger
{

private:
    static const char *getlabel(const LEVEL level)
    {
        const char *label = "";
        switch (level)
        {
        case LEVEL::INFO:
            label = "INFO";
            break;
        case LEVEL::DEBUG:
            label = "DEBUG";
            break;
        case LEVEL::VERBOSE:
            label = "VERBOSE";
            break;
        case LEVEL::WARNING:
            label = "WARNING";
            break;
        case LEVEL::ERROR:
            label = "WARNING";
            break;

        default:
            label = "VERBOSE"; // set default log level as verbose
            break;
        }
        return label;
    }

    Logger();
    ~Logger();

    Logger(const Logger &);
    Logger &operator=(const Logger &);

private:
    static string getCurrentTimestamp()
    {
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        char buffer[100];
        strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y", timeinfo);
        return string(buffer);
    }

public:
    template <typename... T>
    static void log(LEVEL level, const char *message, T... arg)
    {
        const char *label = getlabel(level);
        FILE *fp = fopen("Logs.txt", "a");
        if (!fp)
        {
            perror("File open failed");
            return;
        }

        string timestamp = getCurrentTimestamp();

        // console output : <TIMESTAMP> [LEVEL] <MESSAGE>
        // log entry : [LEVEL] <TIMESTAMP> <MESSAGE>
        cout << timestamp << " ";
        printf("[%s] ", label);

        fprintf(fp, "[%s] ", label);
        fprintf(fp, " %s  ", timestamp.c_str());

        printf(message, arg...);
        printf("\n");

        fprintf(fp, message, arg...);
        fprintf(fp, "\n");

        fclose(fp);
    }
};