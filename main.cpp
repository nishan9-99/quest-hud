// ============================================================================
//  QUEST HUD - a game-HUD terminal task tracker
//  Pure C++17, single file, zero dependencies.
//
//  Your to-do list, but it's a game: add quests, clear them for XP,
//  level up, and keep your daily streak alive.
// ============================================================================

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// ------------------------------ ANSI styling --------------------------------
const char* RESET   = "\033[0m";
const char* BOLD    = "\033[1m";
const char* DIM     = "\033[2m";
const char* GREEN   = "\033[32m";
const char* YELLOW  = "\033[33m";
const char* RED     = "\033[31m";
const char* MAGENTA = "\033[35m";
const char* CYAN    = "\033[36m";
const char* WHITE   = "\033[37m";

const char* kSaveFile = "questhud_save.txt";

void enableAnsiOnWindows() {
#ifdef _WIN32
    // Old Windows consoles need VT processing + UTF-8 enabled explicitly.
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
    return buf;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// ------------------------------- Game rules ---------------------------------
struct RankInfo {
    char code;
    const char* name;
    int xp;
    const char* color;
};

const RankInfo kRanks[] = {
    {'D', "SIDE QUEST",  10, DIM},
    {'C', "NORMAL",      25, GREEN},
    {'B', "HARD",        50, CYAN},
    {'A', "EPIC",       100, MAGENTA},
    {'S', "LEGENDARY",  200, YELLOW},
};

const RankInfo* rankOf(char code) {
    for (const auto& r : kRanks)
        if (r.code == code) return &r;
    return &kRanks[1];  // default: C
}

// XP needed to go from `level` to `level + 1`. Grows as you level up.
int xpForNextLevel(int level) { return 50 * level; }

struct Player {
    long totalXp = 0;
    int streak = 0;
    std::string lastDoneDate;
};

int levelOf(const Player& p) {
    int level = 1;
    long spent = 0;
    while (spent + xpForNextLevel(level) <= p.totalXp) {
        spent += xpForNextLevel(level);
        ++level;
    }
    return level;
}

// XP progress inside the current level: (earned, needed).
std::pair<long, long> levelProgress(const Player& p) {
    int level = 1;
    long spent = 0;
    while (spent + xpForNextLevel(level) <= p.totalXp) {
        spent += xpForNextLevel(level);
        ++level;
    }
    return {p.totalXp - spent, xpForNextLevel(level)};
}

// --------------------------------- Data -------------------------------------
struct Quest {
    int id;
    char rank;
    std::string title;
    bool done;
    std::string created;
};

struct State {
    Player player;
    std::vector<Quest> quests;
    int nextId = 1;
};

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '|') out += "/";
        else out += c;
    }
    return out;
}

void save(const State& s) {
    std::ofstream f(kSaveFile);
    if (!f) return;
    f << "QUESTHUD_SAVE v1\n";
    f << "player|" << s.player.totalXp << "|" << s.player.streak << "|"
      << s.player.lastDoneDate << "\n";
    f << "nextid|" << s.nextId << "\n";
    for (const auto& q : s.quests)
        f << "quest|" << q.id << "|" << q.rank << "|" << escape(q.title) << "|"
          << (q.done ? 1 : 0) << "|" << q.created << "\n";
}

State load() {
    State s;
    std::ifstream f(kSaveFile);
    if (!f) return s;  // first run: fresh save
    std::string line;
    while (std::getline(f, line)) {
        std::vector<std::string> parts;
        std::string part;
        std::istringstream ss(line);
        while (std::getline(ss, part, '|')) parts.push_back(part);
        if (parts.empty()) continue;
        if (parts[0] == "player" && parts.size() >= 4) {
            s.player.totalXp = std::stol(parts[1]);
            s.player.streak = std::stoi(parts[2]);
            s.player.lastDoneDate = parts[3];
        } else if (parts[0] == "nextid" && parts.size() >= 2) {
            s.nextId = std::stoi(parts[1]);
        } else if (parts[0] == "quest" && parts.size() >= 6) {
            s.quests.push_back({std::stoi(parts[1]), parts[2].empty() ? 'C' : parts[2][0],
                                parts[3], parts[4] == "1", parts[5]});
        }
    }
    return s;
}

// -------------------------------- HUD drawing -------------------------------
void bar(long value, long max, int width) {
    if (max <= 0) max = 1;
    int filled = static_cast<int>((double)value / max * width);
    filled = std::max(0, std::min(width, filled));
    std::cout << GREEN;
    for (int i = 0; i < filled; ++i) std::cout << "\xE2\x96\x88";      // full block
    std::cout << DIM;
    for (int i = filled; i < width; ++i) std::cout << "\xE2\x96\x91";  // light shade
    std::cout << RESET;
}

void header() {
    const int W = 60;  // inner width of the HUD box
    const std::string top   = "\xE2\x95\x94";  // box-drawing: double line
    const std::string side  = "\xE2\x95\x91";
    const std::string bot   = "\xE2\x95\x9A";
    const std::string botr  = "\xE2\x95\x9D";
    const std::string topr  = "\xE2\x95\x97";
    const std::string hbar  = "\xE2\x95\x90";
    std::string brand = "   Q U E S T   H U D   ";
    std::string tag   = ":: CODE x CREATE x GAME ::";
    int pad = W - (int)brand.size() - (int)tag.size();
    if (pad < 0) pad = 0;
    std::string hline;
    for (int i = 0; i < W; ++i) hline += hbar;
    std::cout << "\033[2J\033[H";  // clear screen
    std::cout << CYAN << BOLD;
    std::cout << top << hline << topr << "\n";
    std::cout << side << GREEN << brand << DIM << WHITE << tag
              << std::string(pad, ' ') << CYAN << BOLD << side << "\n";
    std::cout << bot << hline << botr << "\n" << RESET;
}

void printPlayerLine(const State& s) {
    int level = levelOf(s.player);
    auto prog = levelProgress(s.player);
    std::cout << BOLD << WHITE << "  PLAYER " << RESET << YELLOW << "LVL " << level << RESET
              << "   XP [";
    bar(prog.first, prog.second, 20);
    std::cout << " " << prog.first << "/" << prog.second << "]";
    if (s.player.streak > 0)
        std::cout << "   " << RED << BOLD << "STREAK x" << s.player.streak << RESET;
    std::cout << "\n\n";
}

void printQuests(const State& s) {
    if (s.quests.empty()) {
        std::cout << DIM << "  Quest log empty. Add your first quest with option [2].\n\n" << RESET;
        return;
    }
    std::cout << CYAN << BOLD << "  -- QUEST LOG " << std::string(32, '-') << RESET << "\n";
    for (const auto& q : s.quests) {
        const RankInfo* r = rankOf(q.rank);
        std::cout << "  " << BOLD << "#" << q.id << RESET << "  ";
        std::cout << r->color << "[" << q.rank << "]" << RESET << " ";
        std::cout << (q.done ? std::string(DIM) + "\xE2\x9C\x94 " : std::string(GREEN) + "\xE2\x97\x88 ");
        std::cout << q.title;
        std::cout << DIM << "  (" << r->name << " +" << r->xp << " XP)" << RESET;
        std::cout << "   " << (q.done ? std::string(GREEN) + "CLEARED" : std::string(YELLOW) + "ACTIVE") << RESET << "\n";
    }
    std::cout << "\n";
}

void stats(const State& s) {
    int level = levelOf(s.player);
    auto prog = levelProgress(s.player);
    int cleared = 0;
    for (const auto& q : s.quests) if (q.done) ++cleared;
    std::cout << CYAN << BOLD << "  -- PLAYER CARD " << std::string(30, '-') << RESET << "\n";
    std::cout << BOLD << "  LEVEL     " << RESET << YELLOW << level << RESET << "\n";
    std::cout << BOLD << "  XP        " << RESET;
    bar(prog.first, prog.second, 30);
    std::cout << " " << prog.first << "/" << prog.second << "  (total " << s.player.totalXp << ")\n";
    std::cout << BOLD << "  QUESTS    " << RESET << cleared << " cleared / " << s.quests.size() << " total\n";
    std::cout << BOLD << "  STREAK    " << RESET << RED << s.player.streak << " day" << (s.player.streak == 1 ? "" : "s") << RESET << "\n\n";
}

void levelUpFanfare(int level) {
    std::cout << "\n" << YELLOW << BOLD;
    std::cout << "  *** LEVEL UP! You are now LVL " << level << " ***\n" << RESET << "\n";
}

void addQuest(State& s, char rank, const std::string& title) {
    if (trim(title).empty()) {
        std::cout << RED << "  A quest needs a title.\n" << RESET;
        return;
    }
    s.quests.push_back({s.nextId++, rankOf(rank)->code, title, false, today()});
    save(s);
    const RankInfo* r = rankOf(rank);
    std::cout << GREEN << "  + Quest added: " << RESET << BOLD << title << RESET
              << DIM << "  [" << r->code << " - " << r->name << ", +" << r->xp << " XP]" << RESET << "\n";
}

void completeQuest(State& s, int id) {
    for (auto& q : s.quests) {
        if (q.id == id) {
            if (q.done) {
                std::cout << DIM << "  Quest #" << id << " is already cleared.\n" << RESET;
                return;
            }
            int before = levelOf(s.player);
            q.done = true;
            const RankInfo* r = rankOf(q.rank);
            s.player.totalXp += r->xp;
            // streak: once per day, +1 if yesterday was kept alive
            std::string t = today();
            if (s.player.lastDoneDate != t) {
                if (!s.player.lastDoneDate.empty()) {
                    std::tm tm = {};
                    std::istringstream ds(s.player.lastDoneDate);
                    ds >> std::get_time(&tm, "%Y-%m-%d");
                    std::time_t last = std::mktime(&tm);
                    std::time_t now = std::time(nullptr);
                    double days = std::difftime(now, last) / 86400.0;
                    s.player.streak = (days < 2.0) ? s.player.streak + 1 : 1;
                } else {
                    s.player.streak = 1;
                }
                s.player.lastDoneDate = t;
            }
            save(s);
            std::cout << GREEN << BOLD << "  QUEST CLEARED: " << RESET << q.title
                      << GREEN << "  +" << r->xp << " XP" << RESET << "\n";
            int after = levelOf(s.player);
            if (after > before) levelUpFanfare(after);
            return;
        }
    }
    std::cout << RED << "  No quest with id #" << id << ".\n" << RESET;
}

void removeQuest(State& s, int id) {
    auto it = std::find_if(s.quests.begin(), s.quests.end(),
                           [id](const Quest& q) { return q.id == id; });
    if (it == s.quests.end()) {
        std::cout << RED << "  No quest with id #" << id << ".\n" << RESET;
        return;
    }
    std::cout << YELLOW << "  - Quest abandoned: " << it->title << RESET << "\n";
    s.quests.erase(it);
    save(s);
}

void clearDone(State& s) {
    size_t before = s.quests.size();
    s.quests.erase(std::remove_if(s.quests.begin(), s.quests.end(),
                                  [](const Quest& q) { return q.done; }),
                   s.quests.end());
    save(s);
    std::cout << DIM << "  Swept " << (before - s.quests.size()) << " cleared quests from the log.\n" << RESET;
}

void help() {
    std::cout << CYAN << BOLD << "\n  -- COMMAND LIST " << std::string(29, '-') << RESET << "\n"
              << BOLD << "  questhud" << RESET << "                      interactive HUD mode\n"
              << BOLD << "  questhud add [RANK] \"title\"" << RESET << "  new quest (RANK: D C B A S, default C)\n"
              << BOLD << "  questhud list" << RESET << "                   show the quest log\n"
              << BOLD << "  questhud done <id>" << RESET << "              clear a quest, gain XP\n"
              << BOLD << "  questhud drop <id>" << RESET << "              abandon a quest\n"
              << BOLD << "  questhud sweep" << RESET << "                  remove all cleared quests\n"
              << BOLD << "  questhud stats" << RESET << "                  player card\n\n";
}

std::string readLine() {
    std::string line;
    std::getline(std::cin, line);
    return trim(line);
}

void interactive() {
    State s = load();
    for (;;) {
        header();
        printPlayerLine(s);
        printQuests(s);
        std::cout << CYAN << BOLD << "  -- MENU " << std::string(37, '-') << RESET << "\n"
                  << "  [1] Refresh log   [2] New quest   [3] Clear quest\n"
                  << "  [4] Abandon       [5] Sweep done  [6] Player card\n"
                  << "  [0] Save & exit\n\n"
                  << GREEN << "  > " << RESET;
        std::string choice = readLine();
        std::cout << "\n";
        if (choice == "0" || choice == "q" || choice == "exit" || choice.empty()) break;
        if (choice == "2") {
            std::cout << "  Quest title: ";
            std::string title = readLine();
            if (title.empty()) continue;
            std::cout << "  Rank (D side / C normal / B hard / A epic / S legendary) [C]: ";
            std::string r = readLine();
            char rank = r.empty() ? 'C' : (char)std::toupper((unsigned char)r[0]);
            addQuest(s, rank, title);
        } else if (choice == "3") {
            std::cout << "  Quest id to clear: ";
            try { completeQuest(s, std::stoi(readLine())); }
            catch (...) { std::cout << RED << "  Not a number.\n" << RESET; }
        } else if (choice == "4") {
            std::cout << "  Quest id to abandon: ";
            try { removeQuest(s, std::stoi(readLine())); }
            catch (...) { std::cout << RED << "  Not a number.\n" << RESET; }
        } else if (choice == "5") {
            clearDone(s);
        } else if (choice == "6") {
            stats(s);
        }
        if (choice != "1") {
            std::cout << DIM << "\n  (press Enter)" << RESET;
            readLine();
        }
    }
    save(s);
    std::cout << "\n" << GREEN << "  Progress saved. See you on the next run, player.\n\n" << RESET;
}

}  // namespace

int main(int argc, char** argv) {
    enableAnsiOnWindows();

    if (argc < 2) {
        interactive();
        return 0;
    }

    std::string cmd = argv[1];
    State s = load();

    if (cmd == "add" && argc >= 3) {
        char rank = 'C';
        int titleArg = 2;
        std::string first = argv[2];
        if (first.size() == 1 && std::string("DCBASdcbas").find(first[0]) != std::string::npos && argc >= 4) {
            rank = (char)std::toupper((unsigned char)first[0]);
            titleArg = 3;
        }
        std::ostringstream title;
        for (int i = titleArg; i < argc; ++i) {
            if (i > titleArg) title << " ";
            title << argv[i];
        }
        addQuest(s, rank, title.str());
    } else if (cmd == "list") {
        printPlayerLine(s);
        printQuests(s);
    } else if ((cmd == "done" || cmd == "complete") && argc >= 3) {
        try { completeQuest(s, std::stoi(argv[2])); }
        catch (...) { std::cout << RED << "  Not a number. Usage: questhud done <id>\n" << RESET; }
    } else if ((cmd == "drop" || cmd == "remove" || cmd == "abandon") && argc >= 3) {
        try { removeQuest(s, std::stoi(argv[2])); }
        catch (...) { std::cout << RED << "  Not a number. Usage: questhud drop <id>\n" << RESET; }
    } else if (cmd == "sweep" || cmd == "clear-done") {
        clearDone(s);
    } else if (cmd == "stats") {
        stats(s);
    } else {
        help();
    }
    return 0;
}
