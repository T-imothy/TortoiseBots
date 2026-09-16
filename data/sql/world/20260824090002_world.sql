-- Additive compatibility for installations that applied the original native
-- migration before the help/zone schema was reconciled.

-- MySQL 8 does not support MariaDB's ADD COLUMN IF NOT EXISTS syntax.
-- The updater executes these statements on one transaction connection.
SET @tb_help_column = IF(
  EXISTS (SELECT 1 FROM information_schema.COLUMNS
          WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='ai_playerbot_help_texts'
            AND COLUMN_NAME='template_changed'),
  'DO 0',
  'ALTER TABLE ai_playerbot_help_texts ADD COLUMN template_changed tinyint unsigned NOT NULL DEFAULT 0 AFTER template_text');
PREPARE tb_help_column_stmt FROM @tb_help_column;
EXECUTE tb_help_column_stmt;
DEALLOCATE PREPARE tb_help_column_stmt;

CREATE TABLE IF NOT EXISTS `ai_playerbot_zone_level` (
  `id` bigint unsigned NOT NULL,
  `level` bigint NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3;
