-- Migration 05: 通讯系统（聊天、邮件等）
-- 职责：结构化存储聊天记录、公会频道、邮件正文。

-- 表 chat_messages：储存全服及私聊的消息体。
CREATE TABLE IF NOT EXISTS chat_messages (
    message_id      BIGSERIAL PRIMARY KEY,
    kingdom_id      INTEGER     NOT NULL REFERENCES kingdoms(kingdom_id) ON DELETE CASCADE,
    channel         VARCHAR(16) NOT NULL,
    sender_player_id BIGINT     NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    receiver_guild_id BIGINT,
    receiver_player_id  BIGINT,
    content         TEXT        NOT NULL,
    attachments     JSONB,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE chat_messages IS '全服/区域/私聊等聊天记录';
COMMENT ON COLUMN chat_messages.message_id IS '聊天记录主键 ID';
COMMENT ON COLUMN chat_messages.kingdom_id IS '消息所属王国';
COMMENT ON COLUMN chat_messages.channel IS 'global/guild/private 等频道分组';
COMMENT ON COLUMN chat_messages.sender_player_id IS '发送者玩家 ID';
COMMENT ON COLUMN chat_messages.receiver_guild_id IS '接收方公会 ID（频道消息）';
COMMENT ON COLUMN chat_messages.receiver_player_id IS '接收方玩家 ID（私聊）';
COMMENT ON COLUMN chat_messages.content IS '文本内容';
COMMENT ON COLUMN chat_messages.attachments IS '附带的道具或元数据';
COMMENT ON COLUMN chat_messages.created_at IS '消息创建时间';

CREATE INDEX IF NOT EXISTS idx_chat_messages_channel_time
    ON chat_messages(channel, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_chat_messages_receiver
    ON chat_messages(receiver_player_id);

-- 表 guild_chat_messages：公会频道专用聊天记录。
CREATE TABLE IF NOT EXISTS guild_chat_messages (
    message_id      BIGSERIAL PRIMARY KEY,
    guild_id        BIGINT      NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,
    sender_player_id BIGINT     NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    content         TEXT        NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE guild_chat_messages IS '公会频道聊天，便于单独索引';
COMMENT ON COLUMN guild_chat_messages.message_id IS '公会消息主键';
COMMENT ON COLUMN guild_chat_messages.guild_id IS '所属公会 ID';
COMMENT ON COLUMN guild_chat_messages.sender_player_id IS '消息发送者';
COMMENT ON COLUMN guild_chat_messages.content IS '聊天文本';
COMMENT ON COLUMN guild_chat_messages.created_at IS '发送时间';
CREATE INDEX IF NOT EXISTS idx_guild_chat_messages_time
    ON guild_chat_messages(guild_id, created_at DESC);

-- 表 mail_templates：定义系统邮件模板及多语言内容。
CREATE TABLE IF NOT EXISTS mail_templates (
    template_id     BIGSERIAL PRIMARY KEY,
    locale          VARCHAR(16) NOT NULL DEFAULT 'en',
    title           VARCHAR(128) NOT NULL,
    body            TEXT        NOT NULL,
    attachments     JSONB,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE mail_templates IS '系统邮件模板，支持多语言';
COMMENT ON COLUMN mail_templates.template_id IS '模板主键 ID';
COMMENT ON COLUMN mail_templates.locale IS '模板语言';
COMMENT ON COLUMN mail_templates.title IS '模板标题';
COMMENT ON COLUMN mail_templates.body IS '模板全文内容';
COMMENT ON COLUMN mail_templates.attachments IS '附加奖励/道具';
COMMENT ON COLUMN mail_templates.created_at IS '模板创建时间';

-- 表 player_mails：玩家个人邮件与系统发信记录。
CREATE TABLE IF NOT EXISTS player_mails (
    mail_id         BIGSERIAL PRIMARY KEY,
    player_id       BIGINT      NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    template_id     BIGINT      REFERENCES mail_templates(template_id),
    title           VARCHAR(128) NOT NULL,
    body            TEXT        NOT NULL,
    attachments     JSONB,
    mail_type       VARCHAR(16) NOT NULL DEFAULT 'system',
    is_read         BOOLEAN     NOT NULL DEFAULT FALSE,
    expires_at      TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE player_mails IS '玩家私人/系统邮件收件箱';
COMMENT ON COLUMN player_mails.mail_id IS '邮件主键 ID';
COMMENT ON COLUMN player_mails.player_id IS '收件人玩家 ID';
COMMENT ON COLUMN player_mails.template_id IS '引用的模板 ID';
COMMENT ON COLUMN player_mails.title IS '邮件标题';
COMMENT ON COLUMN player_mails.body IS '邮件正文';
COMMENT ON COLUMN player_mails.attachments IS '邮件奖励/附件 JSON';
COMMENT ON COLUMN player_mails.mail_type IS '邮件类型标识';
COMMENT ON COLUMN player_mails.is_read IS '是否已读';
COMMENT ON COLUMN player_mails.expires_at IS '过期时间';
COMMENT ON COLUMN player_mails.created_at IS '邮件创建时间';
CREATE INDEX IF NOT EXISTS idx_player_mails_player_read
    ON player_mails(player_id, is_read);

-- 表 email_content_archive：存档大文本或多语言邮件内容。
CREATE TABLE IF NOT EXISTS email_content_archive (
    content_id      BIGSERIAL PRIMARY KEY,
    source_mail_id  BIGINT      NOT NULL REFERENCES player_mails(mail_id) ON DELETE CASCADE,
    locale          VARCHAR(16) NOT NULL,
    rich_content    JSONB       NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE email_content_archive IS '多语言或富文本邮件内容存档';
COMMENT ON COLUMN email_content_archive.content_id IS '存档记录主键';
COMMENT ON COLUMN email_content_archive.source_mail_id IS '关联的邮件 ID';
COMMENT ON COLUMN email_content_archive.locale IS '存档语言';
COMMENT ON COLUMN email_content_archive.rich_content IS '富文本内容 JSON';
COMMENT ON COLUMN email_content_archive.created_at IS '存档时间';
