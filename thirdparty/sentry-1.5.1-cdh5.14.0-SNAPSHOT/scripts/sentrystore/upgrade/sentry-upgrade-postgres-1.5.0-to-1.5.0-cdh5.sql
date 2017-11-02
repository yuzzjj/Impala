SELECT 'Upgrading Sentry store schema from 1.5.0 to 1.5.0-cdh5';
\i 007-SENTRY-1365.postgres.sql;
\i 008-SENTRY-1569.postgres.sql;
\i 009-SENTRY-1805.postgres.sql;

UPDATE "SENTRY_VERSION" SET "SCHEMA_VERSION"='1.5.0-cdh5', "VERSION_COMMENT"='Sentry release version 1.5.0-cdh5' WHERE "VER_ID"=1;
SELECT 'Finished upgrading Sentry store schema from 1.5.0 to 1.5.0-cdh5';
