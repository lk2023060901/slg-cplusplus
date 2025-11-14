//
// Copyright (c) 2009-2010 Mikko Mononen
//
// Provided under zlib license; see original header for details.
//

#include "navigation/recast/BuildContext.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

BuildContext::BuildContext() : rcContext(false), message_count_(0), text_pool_size_(0) {
    resetLog();
}

void BuildContext::dumpLog(const char* format, ...) {
    if (!message_count_) {
        return;
    }
    va_list ap;
    va_start(ap, format);
    std::vprintf(format, ap);
    va_end(ap);
    std::printf("\n");
    for (int i = 0; i < message_count_; ++i) {
        std::printf("  %s\n", messages_[i]);
    }
    message_count_ = 0;
}

int BuildContext::getLogCount() const {
    return message_count_;
}

const char* BuildContext::getLogText(const int i) const {
    if (i < 0 || i >= message_count_) {
        return nullptr;
    }
    return messages_[i];
}

void BuildContext::doResetLog() {
    message_count_ = 0;
    text_pool_size_ = 0;
}

void BuildContext::doLog(const rcLogCategory /*category*/, const char* msg, int len) {
    if (!len) {
        return;
    }
    if (message_count_ >= MAX_MESSAGES) {
        return;
    }
    const int copy_len = rcMin(len + 1, TEXT_POOL_SIZE - text_pool_size_);
    if (copy_len <= 0) {
        return;
    }
    char* dst = &text_pool_[text_pool_size_];
    std::strncpy(dst, msg, copy_len);
    dst[copy_len - 1] = '\0';
    messages_[message_count_++] = dst;
    text_pool_size_ += copy_len;
}

