/* Artwork and control data from Arcade Database (adb.arcadeitalia.net, by
 * motoschifo; most images from ProgettoSnaps). Built only with
 * ARCADE_SHELF_ADB, after the site's author has agreed to this use.
 *
 * Follows the service's rules (adb.arcadeitalia.net/service_scraper.php):
 * one connection at a time, romsets queried in batches with URLs of at
 * most 800 characters, the source credited wherever the data is shown. It
 * downloads only what is missing, to <data>/snap, titles, marquees and
 * adb/<set>.json, on the user's request. */
#ifndef ARCADE_SHELF_ADB_SCRAPER_H
#define ARCADE_SHELF_ADB_SCRAPER_H

#include <string>
#include <vector>

/* The credit line to show with the downloaded data. */
#define ADB_CREDIT "Arcade Database by motoschifo (adb.arcadeitalia.net) - images from ProgettoSnaps"

struct AdbProgress {
	bool running, finished, cancelled;
	int sets_done, sets_total, images; /* images downloaded so far */
	std::string current, error;        /* set in progress; last error */
};

/* Starts downloading on a background thread; false if one is running.
 * ca_file: a CA bundle for HTTPS where the system has none (the Vita).
 * user_agent names the application to the service. */
bool adb_scraper_start(const std::string &data_root, const std::vector<std::string> &sets,
	const std::string &ca_file, const std::string &user_agent);
/* A copy of the current progress. */
AdbProgress adb_scraper_progress(void);
/* Asks the thread to stop after the current file. */
void adb_scraper_cancel(void);
/* Waits for the thread to end (on exit). */
void adb_scraper_join(void);

#endif
