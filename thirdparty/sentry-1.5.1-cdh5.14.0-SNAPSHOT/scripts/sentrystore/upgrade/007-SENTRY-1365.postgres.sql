-- Table "AUTHZ_PATHS_MAPPING" for classes [org.apache.sentry.provider.db.service.model.MAuthzPathsMapping]
CREATE TABLE "AUTHZ_PATHS_MAPPING"
(
    "AUTHZ_OBJ_ID" SERIAL,
    "AUTHZ_OBJ_NAME" varchar(384) NULL,
    "CREATE_TIME_MS" int8 NOT NULL,
    "AUTHZ_SNAPSHOT_ID" bigint NOT NULL,
    CONSTRAINT "AUTHZ_PATHS_MAPPING_PK" PRIMARY KEY ("AUTHZ_OBJ_ID")
);

-- Constraints for table "AUTHZ_PATHS_MAPPING" for class(es) [org.apache.sentry.provider.db.service.model.MAuthzPathsMapping]
CREATE UNIQUE INDEX "AUTHZOBJNAME" ON "AUTHZ_PATHS_MAPPING" ("AUTHZ_OBJ_NAME");

-- Table `AUTHZ_PATH` for classes [org.apache.sentry.provider.db.service.model.MPath]
CREATE TABLE "AUTHZ_PATH"
 (
    "PATH_ID" BIGINT NOT NULL,
    "PATH_NAME" varchar(4000),
    "AUTHZ_OBJ_ID" BIGINT
);

-- Constraints for table `AUTHZ_PATH`
ALTER TABLE "AUTHZ_PATH"
  ADD CONSTRAINT "AUTHZ_PATH_PK" PRIMARY KEY ("PATH_ID");

ALTER TABLE "AUTHZ_PATH"
  ADD CONSTRAINT "AUTHZ_PATH_FK"
  FOREIGN KEY ("AUTHZ_OBJ_ID") REFERENCES "AUTHZ_PATHS_MAPPING" ("AUTHZ_OBJ_ID") DEFERRABLE;
------------------------------------------------------------------
-- Sequences and SequenceTables