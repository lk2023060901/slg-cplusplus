-- Migration 02: 角色与成长相关表
-- 该模块覆盖角色基本资料、战力/击杀历史、资源采集等日常运营数据。

-- 表 players：记录角色基础资料与当前游戏状态。
CREATE TABLE IF NOT EXISTS players (
    player_id       BIGSERIAL PRIMARY KEY,
    account_id      BIGINT      NOT NULL REFERENCES accounts(account_id) ON DELETE CASCADE,
    server_id       INTEGER     NOT NULL,
    nickname        VARCHAR(64) NOT NULL,
    title           VARCHAR(32),
    level           INTEGER     NOT NULL DEFAULT 1,
    vip_level       INTEGER     NOT NULL DEFAULT 0,
    guild_id        BIGINT,
    power           BIGINT      NOT NULL DEFAULT 0,
    kill_points     BIGINT      NOT NULL DEFAULT 0,
    honor_points    BIGINT      NOT NULL DEFAULT 0,
    city_level      INTEGER     NOT NULL DEFAULT 1,
    last_online_at  TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE players IS '角色主表，对应游戏内一个城市/角色';
COMMENT ON COLUMN players.player_id IS '角色记录主键 ID';
COMMENT ON COLUMN players.account_id IS '所属平台账号 ID';
COMMENT ON COLUMN players.server_id IS '所在游戏服 ID';
COMMENT ON COLUMN players.nickname IS '角色在游戏中的昵称';
COMMENT ON COLUMN players.title IS '角色当前的头衔/称号';
COMMENT ON COLUMN players.level IS '角色等级，用于功能解锁';
COMMENT ON COLUMN players.vip_level IS 'VIP 等级/付费段位';
COMMENT ON COLUMN players.guild_id IS '隶属公会 ID（可为空）';
COMMENT ON COLUMN players.power IS '综合战力数值';
COMMENT ON COLUMN players.kill_points IS '累计击杀积分';
COMMENT ON COLUMN players.honor_points IS '荣誉或功勋值';
COMMENT ON COLUMN players.city_level IS '主城等级';
COMMENT ON COLUMN players.last_online_at IS '最近上线时间';
COMMENT ON COLUMN players.created_at IS '记录创建时间';
COMMENT ON COLUMN players.updated_at IS '记录最近更新时间';

CREATE UNIQUE INDEX IF NOT EXISTS ux_players_account_server
    ON players(account_id, server_id);
CREATE INDEX IF NOT EXISTS idx_players_guild
    ON players(guild_id);

-- 表 player_power_history：记录角色各分项战力的快照历史。
CREATE TABLE IF NOT EXISTS player_power_history (
    snapshot_id     BIGSERIAL PRIMARY KEY,
    player_id       BIGINT  NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    power_value     BIGINT  NOT NULL,
    troops_power    BIGINT  NOT NULL DEFAULT 0,
    building_power  BIGINT  NOT NULL DEFAULT 0,
    tech_power      BIGINT  NOT NULL DEFAULT 0,
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE player_power_history IS '角色战力分项快照，用于活动排行与风控';
COMMENT ON COLUMN player_power_history.snapshot_id IS '战力快照主键 ID';
COMMENT ON COLUMN player_power_history.player_id IS '对应的角色 ID';
COMMENT ON COLUMN player_power_history.power_value IS '快照时的总战力值';
COMMENT ON COLUMN player_power_history.troops_power IS '部队战力构成值';
COMMENT ON COLUMN player_power_history.building_power IS '建筑战力构成值';
COMMENT ON COLUMN player_power_history.tech_power IS '科技战力构成值';
COMMENT ON COLUMN player_power_history.occurred_at IS '快照采集时间';
CREATE INDEX IF NOT EXISTS idx_player_power_history_player_time
    ON player_power_history(player_id, occurred_at DESC);

-- 表 player_kill_history：按兵阶记录角色的击杀数量。
CREATE TABLE IF NOT EXISTS player_kill_history (
    snapshot_id     BIGSERIAL PRIMARY KEY,
    player_id       BIGINT  NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    tier_1_kills    BIGINT  NOT NULL DEFAULT 0,
    tier_2_kills    BIGINT  NOT NULL DEFAULT 0,
    tier_3_kills    BIGINT  NOT NULL DEFAULT 0,
    tier_4_kills    BIGINT  NOT NULL DEFAULT 0,
    tier_5_kills    BIGINT  NOT NULL DEFAULT 0,
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE player_kill_history IS '角色各兵阶的击杀统计历史';
COMMENT ON COLUMN player_kill_history.snapshot_id IS '击杀统计快照主键';
COMMENT ON COLUMN player_kill_history.player_id IS '所属角色 ID';
COMMENT ON COLUMN player_kill_history.tier_1_kills IS '击杀 1 阶部队数量';
COMMENT ON COLUMN player_kill_history.tier_2_kills IS '击杀 2 阶部队数量';
COMMENT ON COLUMN player_kill_history.tier_3_kills IS '击杀 3 阶部队数量';
COMMENT ON COLUMN player_kill_history.tier_4_kills IS '击杀 4 阶部队数量';
COMMENT ON COLUMN player_kill_history.tier_5_kills IS '击杀 5 阶部队数量';
COMMENT ON COLUMN player_kill_history.occurred_at IS '统计发生时间';

-- 表 player_resource_collection：记录玩家的资源采集明细。
CREATE TABLE IF NOT EXISTS player_resource_collection (
    record_id       BIGSERIAL PRIMARY KEY,
    player_id       BIGINT  NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    resource_type   VARCHAR(16) NOT NULL,
    gathered_amount BIGINT      NOT NULL,
    tile_level      INTEGER,
    gathered_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE player_resource_collection IS '角色采集记录，用于任务/活动统计';
COMMENT ON COLUMN player_resource_collection.record_id IS '采集记录主键 ID';
COMMENT ON COLUMN player_resource_collection.player_id IS '角色 ID';
COMMENT ON COLUMN player_resource_collection.resource_type IS '采集的资源类型';
COMMENT ON COLUMN player_resource_collection.gathered_amount IS '采集获得的资源数量';
COMMENT ON COLUMN player_resource_collection.tile_level IS '采集的资源地等级';
COMMENT ON COLUMN player_resource_collection.gathered_at IS '采集发生时间';
CREATE INDEX IF NOT EXISTS idx_player_resource_collection_player_type
    ON player_resource_collection(player_id, resource_type);

-- 表 player_city_upgrades：追踪玩家建筑升级的时间线。
CREATE TABLE IF NOT EXISTS player_city_upgrades (
    upgrade_id      BIGSERIAL PRIMARY KEY,
    player_id       BIGINT  NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    building_type   VARCHAR(32) NOT NULL,
    from_level      INTEGER NOT NULL,
    to_level        INTEGER NOT NULL,
    started_at      TIMESTAMPTZ,
    completed_at    TIMESTAMPTZ
);
COMMENT ON TABLE player_city_upgrades IS '建筑升级流水，便于回放和活动判断';
COMMENT ON COLUMN player_city_upgrades.upgrade_id IS '升级流水主键 ID';
COMMENT ON COLUMN player_city_upgrades.player_id IS '执行升级的角色 ID';
COMMENT ON COLUMN player_city_upgrades.building_type IS '建筑类别标识';
COMMENT ON COLUMN player_city_upgrades.from_level IS '升级前的等级';
COMMENT ON COLUMN player_city_upgrades.to_level IS '升级后的等级';
COMMENT ON COLUMN player_city_upgrades.started_at IS '升级开始时间';
COMMENT ON COLUMN player_city_upgrades.completed_at IS '升级完成时间（可为空）';

CREATE INDEX IF NOT EXISTS idx_player_city_upgrades_player_building
    ON player_city_upgrades(player_id, building_type);
