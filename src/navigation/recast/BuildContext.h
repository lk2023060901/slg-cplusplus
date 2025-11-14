//
// Copyright (c) 2009-2010 Mikko Mononen
//
// Provided under zlib license; see original header for details.
//

#pragma once

#include "Recast.h"

/// 极简 Recast 构建上下文，仅负责日志收集。
class BuildContext : public rcContext {
    static const int MAX_MESSAGES = 1000;
    const char* messages_[MAX_MESSAGES];
    int message_count_;
    static const int TEXT_POOL_SIZE = 8000;
    char text_pool_[TEXT_POOL_SIZE];
    int text_pool_size_;

public:
    BuildContext();

    void dumpLog(const char* format, ...);
    int getLogCount() const;
    const char* getLogText(int index) const;

protected:
    void doResetLog() override;
    void doLog(const rcLogCategory category, const char* msg, int len) override;
};

