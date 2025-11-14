-- Migration 06: 角色资产（英雄、物品、部队、任务等）
-- 尽量拆分不同职责的表，减少大字段堆积。

-- 表 heroes：玩家拥有的英雄实例信息。
CREATE TABLE IF NOT EXISTS heroes (
    hero_instance_id BIGSERIAL PRIMARY KEY,
    player_id        BIGINT      NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    hero_config_id   INTEGER     NOT NULL,
    level            INTEGER     NOT NULL DEFAULT 1,
    star_level       INTEGER     NOT NULL DEFAULT 0,
    experience       BIGINT      NOT NULL DEFAULT 0,
    skills           JSONB,
    equipped_items   JSONB,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE heroes IS '玩家英雄实例数据';
COMMENT ON COLUMN heroes.hero_instance_id IS '英雄实例主键 ID';
COMMENT ON COLUMN heroes.player_id IS '所属玩家 ID';
COMMENT ON COLUMN heroes.hero_config_id IS '英雄配置/模板 ID';
COMMENT ON COLUMN heroes.level IS '英雄等级';
COMMENT ON COLUMN heroes.star_level IS '英雄星级';
COMMENT ON COLUMN heroes.experience IS '英雄经验值';
COMMENT ON COLUMN heroes.skills IS '技能升级信息 JSON';
COMMENT ON COLUMN heroes.equipped_items IS '穿戴的装备 JSON';
COMMENT ON COLUMN heroes.created_at IS '实例创建时间';
CREATE INDEX IF NOT EXISTS idx_heroes_player
    ON heroes(player_id);

-- 表 inventory_items：玩家背包中的物品及来源。
CREATE TABLE IF NOT EXISTS inventory_items (
    item_instance_id BIGSERIAL PRIMARY KEY,
    player_id        BIGINT      NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    item_config_id   INTEGER     NOT NULL,
    quantity         INTEGER     NOT NULL DEFAULT 1,
    acquired_from    VARCHAR(32),
    acquired_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE inventory_items IS '玩家背包物品及来源';
COMMENT ON COLUMN inventory_items.item_instance_id IS '物品实例主键';
COMMENT ON COLUMN inventory_items.player_id IS '所属玩家 ID';
COMMENT ON COLUMN inventory_items.item_config_id IS '物品配置 ID';
COMMENT ON COLUMN inventory_items.quantity IS '堆叠数量';
COMMENT ON COLUMN inventory_items.acquired_from IS '获取来源渠道';
COMMENT ON COLUMN inventory_items.acquired_at IS '获得时间';

-- 表 armies：玩家预设的部队组成。
CREATE TABLE IF NOT EXISTS armies (
    army_id         BIGSERIAL PRIMARY KEY,
    player_id       BIGINT      NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    preset_name     VARCHAR(32),
    troop_type      VARCHAR(32) NOT NULL,
    troop_count     BIGINT      NOT NULL,
    load_capacity   BIGINT      NOT NULL DEFAULT 0,
    formation_json  JSONB,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE armies IS '玩家编制的部队信息，包含行军预设';
COMMENT ON COLUMN armies.army_id IS '部队预设主键';
COMMENT ON COLUMN armies.player_id IS '所属玩家 ID';
COMMENT ON COLUMN armies.preset_name IS '编制名称';
COMMENT ON COLUMN armies.troop_type IS '主力兵种类型';
COMMENT ON COLUMN armies.troop_count IS '部队兵力数量';
COMMENT ON COLUMN armies.load_capacity IS '部队行军负重';
COMMENT ON COLUMN armies.formation_json IS '编制阵型/英雄配置 JSON';
COMMENT ON COLUMN armies.updated_at IS '最近更新时间';

-- 表 scouting_reports：侦查目标的报告内容。
CREATE TABLE IF NOT EXISTS scouting_reports (
    report_id       BIGSERIAL PRIMARY KEY,
    player_id       BIGINT      NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    target_object_id BIGINT     REFERENCES map_objects(object_id),
    report_type     VARCHAR(16) NOT NULL,
    summary         TEXT        NOT NULL,
    data            JSONB,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE scouting_reports IS '斥候/侦查报告，便于战斗前情收集';
COMMENT ON COLUMN scouting_reports.report_id IS '侦查报告主键';
COMMENT ON COLUMN scouting_reports.player_id IS '报告所有者玩家 ID';
COMMENT ON COLUMN scouting_reports.target_object_id IS '被侦查的地图对象 ID';
COMMENT ON COLUMN scouting_reports.report_type IS '报告类型，如 scout/scout_defense';
COMMENT ON COLUMN scouting_reports.summary IS '报告摘要文本';
COMMENT ON COLUMN scouting_reports.data IS '详细数据 JSON';
COMMENT ON COLUMN scouting_reports.created_at IS '报告生成时间';

-- 表 transport_orders：资源运输与援助任务。
CREATE TABLE IF NOT EXISTS transport_orders (
    transport_id    BIGSERIAL PRIMARY KEY,
    sender_player_id BIGINT     NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    receiver_player_id BIGINT   NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    resource_type   VARCHAR(16) NOT NULL,
    amount          BIGINT      NOT NULL,
    departed_at     TIMESTAMPTZ,
    arrived_at      TIMESTAMPTZ,
    status          VARCHAR(16) NOT NULL DEFAULT 'scheduled'
);
COMMENT ON TABLE transport_orders IS '资源运输指令';
COMMENT ON COLUMN transport_orders.transport_id IS '运输单主键';
COMMENT ON COLUMN transport_orders.sender_player_id IS '发起运输的玩家 ID';
COMMENT ON COLUMN transport_orders.receiver_player_id IS '接收资源的玩家 ID';
COMMENT ON COLUMN transport_orders.resource_type IS '资源类型';
COMMENT ON COLUMN transport_orders.amount IS '运输的资源数量';
COMMENT ON COLUMN transport_orders.departed_at IS '发车时间';
COMMENT ON COLUMN transport_orders.arrived_at IS '抵达时间';
COMMENT ON COLUMN transport_orders.status IS '运输状态';
CREATE INDEX IF NOT EXISTS idx_transport_orders_sender
    ON transport_orders(sender_player_id, departed_at DESC);

-- 表 tasks：追踪玩家任务及目标完成度。
CREATE TABLE IF NOT EXISTS tasks (
    task_id         BIGSERIAL PRIMARY KEY,
    player_id       BIGINT      NOT NULL REFERENCES players(player_id) ON DELETE CASCADE,
    task_type       VARCHAR(32) NOT NULL,
    status          VARCHAR(16) NOT NULL DEFAULT 'pending',
    progress        INTEGER     NOT NULL DEFAULT 0,
    target_value    INTEGER     NOT NULL DEFAULT 0,
    reward_json     JSONB,
    expires_at      TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE tasks IS '日常/活动任务进度';
COMMENT ON COLUMN tasks.task_id IS '任务记录主键';
COMMENT ON COLUMN tasks.player_id IS '执行任务的玩家 ID';
COMMENT ON COLUMN tasks.task_type IS '任务类型编码';
COMMENT ON COLUMN tasks.status IS '任务状态';
COMMENT ON COLUMN tasks.progress IS '当前进度数值';
COMMENT ON COLUMN tasks.target_value IS '任务目标值';
COMMENT ON COLUMN tasks.reward_json IS '任务奖励描述';
COMMENT ON COLUMN tasks.expires_at IS '任务过期时间';
COMMENT ON COLUMN tasks.created_at IS '任务创建时间';

CREATE INDEX IF NOT EXISTS idx_tasks_player_status
    ON tasks(player_id, status);

-- 表 login_logs：追踪玩家及账号的登录轨迹。
CREATE TABLE IF NOT EXISTS login_logs (
    log_id          BIGSERIAL PRIMARY KEY,
    account_id      BIGINT      NOT NULL REFERENCES accounts(account_id) ON DELETE CASCADE,
    player_id       BIGINT      REFERENCES players(player_id) ON DELETE SET NULL,
    kingdom_id      INTEGER     REFERENCES kingdoms(kingdom_id) ON DELETE SET NULL,
    ip_address      INET,
    device_udid     VARCHAR(128),
    logged_in_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
COMMENT ON TABLE login_logs IS '玩家登录日志，便于追踪登录历史';
COMMENT ON COLUMN login_logs.log_id IS '登录日志主键';
COMMENT ON COLUMN login_logs.account_id IS '登录所用账号 ID';
COMMENT ON COLUMN login_logs.player_id IS '对应的角色 ID（可空）';
COMMENT ON COLUMN login_logs.kingdom_id IS '登录所在王国 ID';
COMMENT ON COLUMN login_logs.ip_address IS '登录 IP 地址';
COMMENT ON COLUMN login_logs.device_udid IS '登录设备唯一标识';
COMMENT ON COLUMN login_logs.logged_in_at IS '登录发生时间';

CREATE INDEX IF NOT EXISTS idx_login_logs_account_time
    ON login_logs(account_id, logged_in_at DESC);
