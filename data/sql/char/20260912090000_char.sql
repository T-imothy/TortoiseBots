-- Durable behavior values. No legacy table or character state is deleted.
CREATE TABLE IF NOT EXISTS `ai_playerbot_values` (
  `bot` int unsigned NOT NULL,
  `event` varchar(45) COLLATE utf8mb3_bin NOT NULL,
  `value` int unsigned NOT NULL,
  `data` varchar(255) NOT NULL DEFAULT '',
  `expires_at` bigint unsigned NOT NULL DEFAULT 0,
  PRIMARY KEY (`bot`, `event`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

-- A host that already ran upstream cleanup has no legacy table. Recreating
-- the empty schema lets this import work there; it cannot recover deleted rows.
CREATE TABLE IF NOT EXISTS `ai_playerbot_random_bots` (
  `id` bigint unsigned NOT NULL AUTO_INCREMENT,
  `owner` bigint unsigned NOT NULL,
  `bot` bigint unsigned NOT NULL,
  `time` bigint NOT NULL,
  `validIn` bigint DEFAULT NULL,
  `event` varchar(45) DEFAULT NULL,
  `value` bigint DEFAULT NULL,
  `data` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `owner` (`owner`),
  KEY `bot` (`bot`),
  KEY `event` (`event`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

-- Import completion is persisted separately from the values. A repeat migration
-- must not resurrect keys deliberately deleted by the new module.
CREATE TABLE IF NOT EXISTS `ai_playerbot_value_import` (
  `source` varchar(45) NOT NULL PRIMARY KEY
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;

-- Old stores could contain duplicate events; the last inserted row wins.
-- Keep newer module values on a repeated import. owner != 0 is not this facade.
-- The six non-expiring legacy keys keep their native expiration exception.
INSERT IGNORE INTO `ai_playerbot_values` (`bot`, `event`, `value`, `data`, `expires_at`)
SELECT old.`bot`, old.`event`, old.`value`, COALESCE(old.`data`, ''),
       CASE WHEN old.`event` IN ('specNo','specLink','init','current_time','always','selfbot')
            THEN 0 ELSE GREATEST(1, old.`time` + COALESCE(old.`validIn`, 0)) END
FROM `ai_playerbot_random_bots` old
JOIN (SELECT MAX(`id`) AS id FROM `ai_playerbot_random_bots`
      WHERE `owner` = 0 AND `event` IS NOT NULL GROUP BY `bot`, `event`) latest
  ON latest.id = old.id
WHERE old.`value` > 0 AND old.`event` <> ''
  AND NOT EXISTS (SELECT 1 FROM `ai_playerbot_value_import` WHERE `source`='legacy_owner_zero_v1');
INSERT IGNORE INTO `ai_playerbot_value_import` (`source`) VALUES ('legacy_owner_zero_v1');
