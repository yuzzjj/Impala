RUN '007-SENTRY-1365.derby.sql';
RUN '008-SENTRY-1569.derby.sql';
RUN '009-SENTRY-1805.derby.sql';

-- Version update

UPDATE SENTRY_VERSION SET SCHEMA_VERSION='1.5.0-cdh5', VERSION_COMMENT='Sentry release version 1.5.0-cdh5' WHERE VER_ID=1;
