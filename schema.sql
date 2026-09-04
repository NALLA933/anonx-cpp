CREATE TABLE IF NOT EXISTS chats (
    chat_id     INTEGER PRIMARY KEY,
    cmd_delete  INTEGER NOT NULL DEFAULT 0,
    admin_play  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS users (
    user_id INTEGER PRIMARY KEY
);

CREATE TABLE IF NOT EXISTS auth (
    chat_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    PRIMARY KEY (chat_id, user_id)
);
CREATE INDEX IF NOT EXISTS idx_auth_chat ON auth (chat_id);

CREATE TABLE IF NOT EXISTS lang (
    chat_id   INTEGER PRIMARY KEY,
    lang_code TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS assistant (
    chat_id INTEGER PRIMARY KEY,
    num     INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS blacklist_chats (
    chat_id INTEGER PRIMARY KEY
);
CREATE TABLE IF NOT EXISTS blacklist_users (
    user_id INTEGER PRIMARY KEY
);

CREATE TABLE IF NOT EXISTS sudoers (
    user_id INTEGER PRIMARY KEY
);

CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
