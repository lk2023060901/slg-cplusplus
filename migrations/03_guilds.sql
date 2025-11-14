-- Migration 03: 公会系统
-- 该模块维持公会、成员及贡献等结构化数据。

-- 表 guilds：公会基础信息与管理设置。
CREATE TABLE IF NOT EXISTS guilds (
    guild_id        BIGSERIAL PRIMARY KEY,
    kingdom_id      INTEGER      NOT NULL,
    name            VARCHAR(50)  NOT NULL,
    abbreviation    VARCHAR(5)   NOT NULL,
    flag_theme      VARCHAR(32),
    description     TEXT,
    leader_player_id BIGINT      NOT NULL REFERENCES players(player_id),
    level           INTEGER      NOT NULL DEFAULT 1,
    member_limit    INTEGER      NOT NULL DEFAULT 50,
    created_at      TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE guilds IS '公会主表，记录公会基础信息';
COMMENT ON COLUMN guilds.guild_id IS '公会主键 ID';
COMMENT ON COLUMN guilds.kingdom_id IS '所属王国/服务器';
COMMENT ON COLUMN guilds.name IS '公会全名';
COMMENT ON COLUMN guilds.abbreviation IS '公会简称/Tag';
COMMENT ON COLUMN guilds.flag_theme IS '旗帜主题或皮肤';
COMMENT ON COLUMN guilds.description IS '公会简介';
COMMENT ON COLUMN guilds.leader_player_id IS '会长玩家 ID';
COMMENT ON COLUMN guilds.level IS '公会等级';
COMMENT ON COLUMN guilds.member_limit IS '成员容量上限';
COMMENT ON COLUMN guilds.created_at IS '创建时间';
COMMENT ON COLUMN guilds.updated_at IS '最近更新时间';

CREATE UNIQUE INDEX IF NOT EXISTS ux_guilds_abbreviation
    ON guilds(kingdom_id, abbreviation);

-- 表 guild_members：维护公会成员身份和贡献。
CREATE TABLE IF NOT EXISTS guild_members (
    guild_id        BIGINT      NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,
    player_id       BIGINT      NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    role_code       VARCHAR(16) NOT NULL DEFAULT 'member',
    join_time       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    contribution    BIGINT      NOT NULL DEFAULT 0,
    last_donate_at  TIMESTAMPTZ,
    PRIMARY KEY (guild_id, player_id)
);
COMMENT ON TABLE guild_members IS '公会成员列表及权限信息';
COMMENT ON COLUMN guild_members.guild_id IS '所属公会 ID';
COMMENT ON COLUMN guild_members.player_id IS '成员玩家 ID';
COMMENT ON COLUMN guild_members.role_code IS '成员权限/职位代码';
COMMENT ON COLUMN guild_members.join_time IS '加入公会时间';
COMMENT ON COLUMN guild_members.contribution IS '累计贡献值';
COMMENT ON COLUMN guild_members.last_donate_at IS '最近一次捐献/帮助时间';

-- 表 guild_statistics：聚合的公会战力、击杀与领地数据。
CREATE TABLE IF NOT EXISTS guild_statistics (
    guild_id        BIGINT  PRIMARY KEY REFERENCES guilds(guild_id) ON DELETE CASCADE,
    total_power     BIGINT  NOT NULL DEFAULT 0,
    total_kills     BIGINT  NOT NULL DEFAULT 0,
    territory_count INTEGER NOT NULL DEFAULT 0,
    gift_level      INTEGER NOT NULL DEFAULT 1,
    last_snapshot_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE guild_statistics IS '公会战力/击杀等聚合统计';
COMMENT ON COLUMN guild_statistics.guild_id IS '关联公会 ID';
COMMENT ON COLUMN guild_statistics.total_power IS '成员总战力汇总';
COMMENT ON COLUMN guild_statistics.total_kills IS '成员总击杀数';
COMMENT ON COLUMN guild_statistics.territory_count IS '当前领地数量';
COMMENT ON COLUMN guild_statistics.gift_level IS '礼物宝箱等级';
COMMENT ON COLUMN guild_statistics.last_snapshot_at IS '统计刷新时间';

-- 表 guild_flags：配置公会的旗帜样式。
CREATE TABLE IF NOT EXISTS guild_flags (
    guild_id        BIGINT  PRIMARY KEY REFERENCES guilds(guild_id) ON DELETE CASCADE,
    flag_icon       VARCHAR(64) NOT NULL,
    flag_color      VARCHAR(16) NOT NULL,
    slogan          VARCHAR(64),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE guild_flags IS '公会旗帜展示配置';
COMMENT ON COLUMN guild_flags.guild_id IS '关联公会 ID';
COMMENT ON COLUMN guild_flags.flag_icon IS '旗帜图案资源';
COMMENT ON COLUMN guild_flags.flag_color IS '主色调';
COMMENT ON COLUMN guild_flags.slogan IS '口号/宣言文案';
COMMENT ON COLUMN guild_flags.updated_at IS '配置更新时间';

-- 表 guild_gifts：记录公会礼物掉落来源。
CREATE TABLE IF NOT EXISTS guild_gifts (
    gift_id         BIGSERIAL PRIMARY KEY,
    guild_id        BIGINT      NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,
    source_type     VARCHAR(16) NOT NULL,
    source_id       BIGINT,
    rarity          SMALLINT    NOT NULL DEFAULT 1,
    delivered_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE guild_gifts IS '公会礼物掉落记录，用于礼物等级与奖励';
COMMENT ON COLUMN guild_gifts.gift_id IS '礼物记录主键';
COMMENT ON COLUMN guild_gifts.guild_id IS '领取礼物的公会 ID';
COMMENT ON COLUMN guild_gifts.source_type IS '礼物来源类型';
COMMENT ON COLUMN guild_gifts.source_id IS '来源对象 ID（可选）';
COMMENT ON COLUMN guild_gifts.rarity IS '礼物稀有度';
COMMENT ON COLUMN guild_gifts.delivered_at IS '发放时间';
CREATE INDEX IF NOT EXISTS idx_guild_gifts
    ON guild_gifts(guild_id, delivered_at DESC);

-- 表 guild_help_requests：跟踪成员发起的帮助请求。
CREATE TABLE IF NOT EXISTS guild_help_requests (
    help_id         BIGSERIAL PRIMARY KEY,
    guild_id        BIGINT  NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,
    requester_id    BIGINT  NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    building_type   VARCHAR(32) NOT NULL,
    target_level    INTEGER NOT NULL,
    total_helps     INTEGER NOT NULL DEFAULT 0,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE guild_help_requests IS '公会帮助请求，用于捐助/加速';
COMMENT ON COLUMN guild_help_requests.help_id IS '帮助请求主键';
COMMENT ON COLUMN guild_help_requests.guild_id IS '请求所属的公会 ID';
COMMENT ON COLUMN guild_help_requests.requester_id IS '发起请求的玩家 ID';
COMMENT ON COLUMN guild_help_requests.building_type IS '请求帮助的建筑类别';
COMMENT ON COLUMN guild_help_requests.target_level IS '目标等级';
COMMENT ON COLUMN guild_help_requests.total_helps IS '当前已获得的帮助次数';
COMMENT ON COLUMN guild_help_requests.created_at IS '请求创建时间';

CREATE INDEX IF NOT EXISTS idx_guild_help
    ON guild_help_requests(guild_id, created_at DESC);

-- 表 guild_announcements：管理公会公告与作战计划。
CREATE TABLE IF NOT EXISTS guild_announcements (
    announcement_id BIGSERIAL PRIMARY KEY,
    guild_id        BIGINT  NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,
    author_id       BIGINT  NOT NULL REFERENCES players(player_id) ON DELETE SET NULL,
    title           VARCHAR(64) NOT NULL,
    body            TEXT        NOT NULL,
    pinned          BOOLEAN     NOT NULL DEFAULT FALSE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE guild_announcements IS '公会公告与作战计划';
COMMENT ON COLUMN guild_announcements.announcement_id IS '公告主键 ID';
COMMENT ON COLUMN guild_announcements.guild_id IS '所属公会 ID';
COMMENT ON COLUMN guild_announcements.author_id IS '发布公告的玩家 ID';
COMMENT ON COLUMN guild_announcements.title IS '公告标题';
COMMENT ON COLUMN guild_announcements.body IS '公告正文内容';
COMMENT ON COLUMN guild_announcements.pinned IS '是否置顶';
COMMENT ON COLUMN guild_announcements.created_at IS '公告创建时间';
