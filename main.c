#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>

#include <pthread.h>

#include "timer.h"
#include "jsmn.h"

struct job {
    char *startP;
    long start;
    char *endP;
    long end;
    int jobId;
};

struct parseResult {
    long lines;
    struct timespec time;
};

void *parseTokens(void *);

long findNextNewline(long, long);

jsmntok_t *getbykey(const char *, int, const char *, jsmntok_t *, int);

pthread_t *threads;
struct job *jobs;
struct parseResult *results;

int main(int argc, char **argv) {
    char *filename = NULL;
    int parse = 1;
    int threadCount = 8;
    int c;
    while ((c = getopt(argc, argv, "f:nj:")) != -1) {
        switch (c) {
            case 'f':
                filename = optarg;
                break;
            case 'n':
                parse = 0;
                break;
            case 'j': {
                char *end;
                threadCount = (int) strtol(optarg, &end, 10);
                if (end != optarg + strlen(optarg)) {
                    threadCount = 4;
                    printf("Invalid thread count specified. Defaulting to 4\n");
                }
                break;
            }
            default:
                break;
        }
    }

    if (filename == NULL) {
        printf("The -f argument is required\n");
        return 1;
    }

    struct stat buffer;

    int file = open(filename, O_RDONLY);

    if (file < 0) {
        printf("Could not open file.\n");
        return 1;
    }


    printf("Reading file %s\n", filename);

    timekeeper_t timer;
    starttimer(&timer);

    fstat(file, &buffer);
    long size = buffer.st_size;

    char *strBuf = (char *) malloc(sizeof(char) * size);

    long bytesRead;
    long totalRead = 0;
    while ((bytesRead = read(file, strBuf + totalRead, size)) != 0) {
        totalRead += bytesRead;
    }
    stoptimer(&timer);
    double mbs = ((double) totalRead / (double) (timer.seconds * 1000000000 + timer.nanos)) * 1000;
    printf("Read %li bytes in %li.%03li seconds, at ~%0.2lfMb/s\n", totalRead, timer.seconds, timer.nanos / 1000000,
           mbs);


    timekeeper_t totalTimer;
    starttimer(&totalTimer);

    if (parse) {
        printf("Parsing file using %d threads.\n", threadCount);
        threads = malloc(sizeof(pthread_t) * threadCount);
        jobs = malloc(sizeof(struct job) * threadCount);
        results = malloc(sizeof(struct parseResult) * threadCount);
        long proxSize = size / threadCount;
        char *currentPointer = strBuf;
        for (int i = 0; i < threadCount; i++) {
            jobs[i].jobId = i;
            if (i == threadCount - 1) {
                jobs[i].startP = currentPointer;
                jobs[i].endP = strBuf + size;
            } else {
                jobs[i].startP = currentPointer;
                jobs[i].endP = jobs[i].startP + proxSize;
                while (*jobs[i].endP++ != '\n');
                currentPointer = jobs[i].endP;
            }
        }

        for (int i = 0; i < threadCount; i++) {
            pthread_create(&threads[i], NULL, &parseTokens, &jobs[i]);
        }
    }

    long totalLines = 0;
    for (int i = 0; i < threadCount; i++) {
        pthread_join(threads[i], NULL);
        totalLines += results[i].lines;
    }

    stoptimer(&totalTimer);

    printf("Parsed a total of %li lines in %li.%03li seconds.\n", totalLines, totalTimer.seconds,
           totalTimer.nanos / 1000000);

    free(strBuf);
    return 0;
}

jsmntok_t *getbykey(const char *key, int strlen, const char *jsonstring, jsmntok_t *tokens, int toklength) {
    for (int i = 0; i < toklength; i++) {
        jsmntok_t token = tokens[i];
        if (token.type == JSMN_STRING && strlen == token.end - token.start &&
            strncmp(jsonstring + token.start, key, token.end - token.start) == 0) {
            return &tokens[i + 1];
        }
    }
    return NULL;
}

void *parseTokens(void *collection) {
    struct job *pointers = collection;
    //printf("Parsing tokens from %li characters in job %d.\n", pointers->endP - pointers->startP, pointers->jobId);

    long totalTokenCount = 0;

    char *buffer = pointers->startP;
    long tokenCount = 0;
    int lines = 0;
    timekeeper_t timer;
    starttimer(&timer);
    do {
        int start = 0;
        int end = 0;

        while (buffer[end++] != '\n');

        jsmntok_t tokens[50];
        jsmn_parser parser;
        jsmn_init(&parser);

        tokenCount = jsmn_parse(&parser, buffer + start, end - 1 - start, tokens, 50);
        totalTokenCount += tokenCount - 1;
        lines++;
        buffer += end;
    } while (tokenCount > 0 && *buffer != EOF && *buffer != 0 && buffer < pointers->endP);
    stoptimer(&timer);
    if (totalTokenCount > 0) {
        //printf("Parsed %li tokens and %d lines in job %d.\n", totalTokenCount / 2, lines, pointers->jobId);
    } else {
        printf("Failed to parse tokens in job %d.\n", pointers->jobId);
    }
    results[pointers->jobId].lines = lines;
    results[pointers->jobId].time.tv_sec = timer.seconds;
    results[pointers->jobId].time.tv_nsec = timer.nanos;

    int exit = 1;
    pthread_exit(&exit);
}

long findNextNewline(long fd, long min) {
    long currentIndex = lseek(fd, 0, SEEK_CUR);
    lseek(fd, min, SEEK_CUR);
    long index = 0;
    char character;
    do {
        read(fd, &character, 1);
        index++;
    } while (character != '\n');
    lseek(fd, currentIndex, SEEK_SET);
    return min + index;
}