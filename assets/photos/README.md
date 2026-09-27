# Candidate portraits

Files are named `<candidate id>.jpg` (IDs are listed in `docs/CANDIDATES.md`). The
dashboard loads them automatically. Candidates without a file get a generated
avatar in their party colour showing their surname.

`CREDITS.json` records where each portrait came from:

- **Legislative Yuan official portraits (22).** Most came through
  [weryk153/legislator-background](https://github.com/weryk153/legislator-background)
  (11th term). Three older ones came from [g0v/ly-tel](https://github.com/g0v/ly-tel).
- **County/city council portraits (6).** From the councils' official websites.
- **Wikimedia Commons images (5).** Incumbent mayors: 68000-01, 10017-01, 10018-01,
  10005-02, 09007-01. The upstream repository did not record the file names or
  authors, so **look up their attribution on Commons before public use**.

To find more portraits on a machine with internet access (licence-checked,
Wikimedia Commons only):

    python3 tools/fetch_photos.py --dry-run
    python3 tools/fetch_photos.py
