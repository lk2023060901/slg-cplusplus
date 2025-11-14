-- Migration 01: 账号体系
-- 本文件定义账号及安全相关表，确保基础数据结构清晰、职责单一。

-- 表 accounts：保存玩家在平台侧的基础资料、状态。
CREATE TABLE IF NOT EXISTS accounts (
    account_id          BIGSERIAL PRIMARY KEY,
    platform_uid        VARCHAR(64)    NOT NULL UNIQUE,
    platform            VARCHAR(32)    NOT NULL,
    region_code         VARCHAR(16)    NOT NULL DEFAULT 'global',
    email               VARCHAR(128),
    status              SMALLINT       NOT NULL DEFAULT 0,
    last_login_at       TIMESTAMPTZ,
    created_at          TIMESTAMPTZ    NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ    NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE accounts IS '玩家账号主表，记录平台账号及基础状态信息';
COMMENT ON COLUMN accounts.account_id IS '账号主键 ID';
COMMENT ON COLUMN accounts.platform_uid IS '平台唯一ID，可为自有账号平台或渠道账号';
COMMENT ON COLUMN accounts.platform IS '注册使用的平台（如 ios/android/pc 等）';
COMMENT ON COLUMN accounts.region_code IS '大区/集群标识，用于跨区部署';
COMMENT ON COLUMN accounts.email IS '账号绑定的邮箱，可为空';
COMMENT ON COLUMN accounts.status IS '账号状态：0=正常，>0 为冻结/封禁等特定状态码';
COMMENT ON COLUMN accounts.last_login_at IS '最后登录时间，便于统计活跃情况';
COMMENT ON COLUMN accounts.created_at IS '账号记录创建时间';
COMMENT ON COLUMN accounts.updated_at IS '账号记录最近一次修改时间';

-- 表 account_devices：记录账号使用过的终端设备。
CREATE TABLE IF NOT EXISTS account_devices (
    device_id       BIGSERIAL PRIMARY KEY,
    account_id      BIGINT      NOT NULL REFERENCES accounts(account_id) ON DELETE CASCADE,
    device_udid     VARCHAR(128) NOT NULL,
    device_model    VARCHAR(64),
    os_version      VARCHAR(32),
    first_seen_at   TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    last_seen_at    TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE account_devices IS '账号登录的终端信息，便于安全策略与统计';
COMMENT ON COLUMN account_devices.device_id IS '终端记录主键 ID';
COMMENT ON COLUMN account_devices.account_id IS '关联的平台账号 ID';
COMMENT ON COLUMN account_devices.device_udid IS '设备唯一标识';
COMMENT ON COLUMN account_devices.device_model IS '设备型号信息';
COMMENT ON COLUMN account_devices.os_version IS '操作系统版本信息';
COMMENT ON COLUMN account_devices.first_seen_at IS '首次检测到该设备的时间';
COMMENT ON COLUMN account_devices.last_seen_at IS '最后一次使用该设备登录的时间';

CREATE UNIQUE INDEX IF NOT EXISTS ux_account_devices_account_udid
    ON account_devices(account_id, device_udid);

-- 表 account_security_events：账号相关的安全审计事件流水。
CREATE TABLE IF NOT EXISTS account_security_events (
    event_id        BIGSERIAL PRIMARY KEY,
    account_id      BIGINT      NOT NULL REFERENCES accounts(account_id) ON DELETE CASCADE,
    event_type      VARCHAR(32) NOT NULL,
    ip_address      INET,
    user_agent      VARCHAR(256),
    metadata        JSONB,
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE account_security_events IS '账号安全事件（登录、改密、封禁等）的审计记录';
COMMENT ON COLUMN account_security_events.event_id IS '安全事件主键 ID';
COMMENT ON COLUMN account_security_events.account_id IS '触发事件的账号 ID';
COMMENT ON COLUMN account_security_events.metadata IS '额外上下文信息，如地区、原因等';
COMMENT ON COLUMN account_security_events.event_type IS '事件类型，如 login/password_change 等';
COMMENT ON COLUMN account_security_events.ip_address IS '事件发生时的客户端 IP';
COMMENT ON COLUMN account_security_events.user_agent IS '客户端 User-Agent 信息';
COMMENT ON COLUMN account_security_events.occurred_at IS '事件发生时间';

CREATE INDEX IF NOT EXISTS idx_account_security_events_account_time
    ON account_security_events(account_id, occurred_at DESC);

-- 表 account_external_tokens：第三方平台令牌，用于登录/支付等回调校验。
CREATE TABLE IF NOT EXISTS account_external_tokens (
    token_id        BIGSERIAL PRIMARY KEY,
    account_id      BIGINT      NOT NULL REFERENCES accounts(account_id) ON DELETE CASCADE,
    provider        VARCHAR(32) NOT NULL,
    provider_uid    VARCHAR(128) NOT NULL,
    access_token    TEXT        NOT NULL,
    refresh_token   TEXT,
    expires_at      TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE account_external_tokens IS '第三方登录/支付令牌信息，供回调核验';
COMMENT ON COLUMN account_external_tokens.token_id IS '令牌记录主键 ID';
COMMENT ON COLUMN account_external_tokens.account_id IS '所属账号 ID';
COMMENT ON COLUMN account_external_tokens.provider IS '第三方提供方标识，例如 apple/google';
COMMENT ON COLUMN account_external_tokens.provider_uid IS '第三方平台上的用户 ID';
COMMENT ON COLUMN account_external_tokens.access_token IS '当前使用的访问令牌内容';
COMMENT ON COLUMN account_external_tokens.refresh_token IS '可选的刷新令牌内容';
COMMENT ON COLUMN account_external_tokens.expires_at IS '令牌过期时间';
COMMENT ON COLUMN account_external_tokens.created_at IS '记录创建时间';

CREATE UNIQUE INDEX IF NOT EXISTS ux_account_external_tokens_provider_uid
    ON account_external_tokens(provider, provider_uid);
