library(DBI); library(duckdb)

# Connect read-only to the Bello DB in the user's home directory
db_path <- paste0(Sys.getenv("HOME"), "/.local/share/bello/bello.db")
con <- dbConnect(duckdb::duckdb(), db_path, read_only = TRUE)

# Schema for items
cat("-- items table schema --\n")
print(dbGetQuery(con, "PRAGMA table_info('items');"))

# Counts
cat("-- counts --\n")
print(dbGetQuery(con, "SELECT COUNT(*) AS n_items FROM items;"))
print(dbGetQuery(con, "SELECT COUNT(*) AS n_collections FROM collections;"))

# Recent items (show relevant fields)
cat("-- recent items --\n")
print(dbGetQuery(con, "SELECT id,title,authors,year,doi,url FROM items ORDER BY ROWID DESC LIMIT 10;"))

# Collections (list a few)
cat("-- collections (sample) --\n")
print(dbGetQuery(con, "SELECT name FROM collections ORDER BY name LIMIT 20;"))

# Recent item->collection links
cat("-- recent item_collections --\n")
print(dbGetQuery(con, "SELECT item_id,collection FROM item_collections ORDER BY item_id DESC LIMIT 20;"))

dbDisconnect(con, shutdown = TRUE)
