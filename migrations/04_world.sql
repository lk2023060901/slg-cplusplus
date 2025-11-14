-- Migration 04: 世界与地图实体
-- 定义王国、圣地、地图对象等数据表，确保字段职责单一。

-- 表 kingdoms：记录各王国/服务器的基础属性。
CREATE TABLE IF NOT EXISTS kingdoms (
    kingdom_id      SERIAL PRIMARY KEY,
    name            VARCHAR(50) NOT NULL,
    region_code     VARCHAR(16) NOT NULL,
    opened_at       TIMESTAMPTZ,
    status          SMALLINT    NOT NULL DEFAULT 0
);
COMMENT ON TABLE kingdoms IS '王国/服务器主表，记录开放与状态信息';
COMMENT ON COLUMN kingdoms.kingdom_id IS '王国主键 ID';
COMMENT ON COLUMN kingdoms.name IS '王国名称';
COMMENT ON COLUMN kingdoms.region_code IS '所属区域标识';
COMMENT ON COLUMN kingdoms.opened_at IS '开服时间';
COMMENT ON COLUMN kingdoms.status IS '王国状态枚举';

-- 表 map_objects：世界地图中的所有对象实例。
CREATE TABLE IF NOT EXISTS map_objects (
    object_id       BIGSERIAL PRIMARY KEY,
    kingdom_id      INTEGER     NOT NULL REFERENCES kingdoms(kingdom_id) ON DELETE CASCADE,
    object_type     VARCHAR(32) NOT NULL,
    owner_player_id BIGINT,
    owner_guild_id BIGINT,
    position_x      INTEGER     NOT NULL,
    position_y      INTEGER     NOT NULL,
    level           INTEGER,
    state           VARCHAR(16) NOT NULL DEFAULT 'idle',
    metadata        JSONB,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE map_objects IS '世界地图上的可交互对象（城市、资源、遗址等）';
COMMENT ON COLUMN map_objects.object_id IS '地图对象主键';
COMMENT ON COLUMN map_objects.kingdom_id IS '所属王国 ID';
COMMENT ON COLUMN map_objects.object_type IS '对象类别';
COMMENT ON COLUMN map_objects.owner_player_id IS '拥有者玩家 ID（可空）';
COMMENT ON COLUMN map_objects.owner_guild_id IS '拥有者公会 ID（可空）';
COMMENT ON COLUMN map_objects.position_x IS '二维坐标 X';
COMMENT ON COLUMN map_objects.position_y IS '二维坐标 Y';
COMMENT ON COLUMN map_objects.level IS '对象等级';
COMMENT ON COLUMN map_objects.state IS '当前状态，如 idle/burning';
COMMENT ON COLUMN map_objects.metadata IS '额外配置/属性 JSON';
COMMENT ON COLUMN map_objects.updated_at IS '最近更新时间';

CREATE INDEX IF NOT EXISTS idx_map_objects_kingdom_type
    ON map_objects(kingdom_id, object_type);

-- 表 holy_sites：圣地、遗迹等特殊据点。
CREATE TABLE IF NOT EXISTS holy_sites (
    site_id         BIGSERIAL PRIMARY KEY,
    kingdom_id      INTEGER     NOT NULL REFERENCES kingdoms(kingdom_id) ON DELETE CASCADE,
    name            VARCHAR(64) NOT NULL,
    site_type       VARCHAR(32) NOT NULL,
    guardian_level  INTEGER     NOT NULL DEFAULT 1,
    occupied_by_guild BIGINT,
    status          VARCHAR(16) NOT NULL DEFAULT 'sealed',
    next_event_at   TIMESTAMPTZ,
    metadata        JSONB
);
COMMENT ON TABLE holy_sites IS '圣地/遗迹/神庙等特殊地图点位';
COMMENT ON COLUMN holy_sites.site_id IS '圣地主键';
COMMENT ON COLUMN holy_sites.kingdom_id IS '所属王国 ID';
COMMENT ON COLUMN holy_sites.name IS '圣地名称';
COMMENT ON COLUMN holy_sites.site_type IS '圣地类别';
COMMENT ON COLUMN holy_sites.guardian_level IS '守军等级';
COMMENT ON COLUMN holy_sites.occupied_by_guild IS '占领公会 ID';
COMMENT ON COLUMN holy_sites.status IS '当前状态，如 sealed/open';
COMMENT ON COLUMN holy_sites.next_event_at IS '下一次事件时间';
COMMENT ON COLUMN holy_sites.metadata IS '额外属性';

-- 表 monuments：王国纪念碑等永久性建筑。
CREATE TABLE IF NOT EXISTS monuments (
    monument_id     BIGSERIAL PRIMARY KEY,
    kingdom_id      INTEGER     NOT NULL REFERENCES kingdoms(kingdom_id) ON DELETE CASCADE,
    title           VARCHAR(64) NOT NULL,
    description     TEXT,
    built_at        TIMESTAMPTZ,
    created_by_guild BIGINT,
    destroyed_at    TIMESTAMPTZ
);
COMMENT ON TABLE monuments IS '王国纪念碑或事件纪念建筑记录';
COMMENT ON COLUMN monuments.monument_id IS '纪念碑主键 ID';
COMMENT ON COLUMN monuments.kingdom_id IS '所在王国 ID';
COMMENT ON COLUMN monuments.title IS '纪念碑标题';
COMMENT ON COLUMN monuments.description IS '纪念碑描述';
COMMENT ON COLUMN monuments.built_at IS '建造时间';
COMMENT ON COLUMN monuments.created_by_guild IS '建造的公会 ID';
COMMENT ON COLUMN monuments.destroyed_at IS '被摧毁时间';

-- 表 kingship_history：记录历任国王及任期。
CREATE TABLE IF NOT EXISTS kingship_history (
    record_id       BIGSERIAL PRIMARY KEY,
    kingdom_id      INTEGER     NOT NULL REFERENCES kingdoms(kingdom_id) ON DELETE CASCADE,
    king_player_id  BIGINT      NOT NULL REFERENCES players(player_id),
    reign_start     TIMESTAMPTZ NOT NULL,
    reign_end       TIMESTAMPTZ,
    coronation_note TEXT
);
COMMENT ON TABLE kingship_history IS '历任国王信息';
COMMENT ON COLUMN kingship_history.record_id IS '国王记录主键';
COMMENT ON COLUMN kingship_history.kingdom_id IS '所属王国 ID';
COMMENT ON COLUMN kingship_history.king_player_id IS '国王玩家 ID';
COMMENT ON COLUMN kingship_history.reign_start IS '登基时间';
COMMENT ON COLUMN kingship_history.reign_end IS '退位时间（可空）';
COMMENT ON COLUMN kingship_history.coronation_note IS '加冕备注';

CREATE INDEX IF NOT EXISTS idx_kingship_kingdom_time
    ON kingship_history(kingdom_id, reign_start DESC);

-- 表 rally_reservations：管理公会发起的集结预约。
CREATE TABLE IF NOT EXISTS rally_reservations (
    reservation_id  BIGSERIAL PRIMARY KEY,
    guild_id        BIGINT      NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,
    target_object_id BIGINT     NOT NULL REFERENCES map_objects(object_id) ON DELETE CASCADE,
    rally_type      VARCHAR(16) NOT NULL,
    rally_time      TIMESTAMPTZ NOT NULL,
    created_by      BIGINT      NOT NULL REFERENCES players(player_id),
    note            VARCHAR(128)
);
COMMENT ON TABLE rally_reservations IS '公会集结/预约信息，用于 HolyLand/Monument 争夺';
COMMENT ON COLUMN rally_reservations.reservation_id IS '集结预约主键 ID';
COMMENT ON COLUMN rally_reservations.guild_id IS '发起公会 ID';
COMMENT ON COLUMN rally_reservations.target_object_id IS '目标地图对象 ID';
COMMENT ON COLUMN rally_reservations.rally_type IS '集结类型，如 rally/defend';
COMMENT ON COLUMN rally_reservations.rally_time IS '预定集结时间';
COMMENT ON COLUMN rally_reservations.created_by IS '创建预约的玩家 ID';
COMMENT ON COLUMN rally_reservations.note IS '备注与策略说明';

CREATE INDEX IF NOT EXISTS idx_rally_reservations_guild_time
    ON rally_reservations(guild_id, rally_time DESC);
