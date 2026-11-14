// tsmart: SDL2 desktop front end for the Tamagotchi Smart emulator.
//
//   tsmart IMAGE.bin [--card CARD.bin]... [--scale N] [--fresh] [--realtime]
//
// The emulator never modifies IMAGE.bin or a card file. The device's flash, including saves, is kept in a working
// copy next to the image (IMAGE.bin.flash), the clock in IMAGE.bin.rtc, and each card in CARD.bin.card (a download
// writes a use record to the card, so delete that file to get an unused card back).
//
// Mouse: the screen is four touch pads (quadrants). Hold the left button about half a second to tap; drag from one
//        half to the other to swipe. Drop a card file on the window to insert it.
// Keys:  Space / Enter  the device's button        Arrows      swipe up / down / left / right
//        Q W / A S      the four pads              C           insert the next card (after the last: no card)
//        Tab (hold)     fast-forward               X           eject the card
//        F5             reset the device           F12         screenshot (BMP next to the image)
//        Esc            quit
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "tsmart.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <ctime>
#include <deque>
#include <vector>

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
	std::ifstream f(path, std::ios::binary); if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), {}); return true;
}
static bool write_file(const std::string &path, const std::vector<uint8_t> &data) {
	std::ofstream f(path, std::ios::binary); if (!f) return false;
	f.write((const char *)data.data(), (std::streamsize)data.size()); return (bool)f;
}
// Quadrant layout found from the firmware's own touch feedback and confirmed against a video of a real unit:
// pad 2 top-left, pad 1 top-right, pad 3 bottom-left, pad 0 bottom-right.
static int pad_at(int x, int y) { return x < 64 ? (y < 64 ? 2 : 3) : (y < 64 ? 1 : 0); }

int main(int argc, char **argv) {
	SDL_SetMainReady();
	std::string image_path; std::vector<std::string> card_paths; int scale = 4; bool fresh = false, realtime = false; long exit_after = -1; std::string shot_at_exit;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--scale" && i + 1 < argc) scale = atoi(argv[++i]);
		else if (a == "--fresh") fresh = true;
		else if (a == "--realtime") realtime = true;                                    // let the clock catch up with the time the emulator was closed
		else if (a == "--card" && i + 1 < argc) card_paths.push_back(argv[++i]);
		else if (a == "--exit-after" && i + 1 < argc) exit_after = atol(argv[++i]);     // for automated tests
		else if (a == "--shot-at-exit" && i + 1 < argc) shot_at_exit = argv[++i];       // for automated tests
		else image_path = a;
	}
	if (image_path.empty()) { fprintf(stderr, "usage: tsmart IMAGE.bin [--card CARD.bin]... [--scale N] [--fresh] [--realtime]\n"); return 2; }
	if (scale < 1 || scale > 12) scale = 4;

	std::vector<uint8_t> image;
	if (!read_file(image_path, image)) { fprintf(stderr, "cannot read %s\n", image_path.c_str()); return 2; }
	if (image.size() != 0x1000000 || memcmp(image.data(), "GP-SPIF-HEADER", 14) != 0) {
		fprintf(stderr, "%s is not a RAW 16 MB Tamagotchi Smart flash image (it must start with GP-SPIF-HEADER)\n", image_path.c_str()); return 2; }
	const std::string flash_path = image_path + ".flash";
	std::vector<uint8_t> working;
	if (!fresh && read_file(flash_path, working) && working.size() == image.size()) fprintf(stderr, "resuming from %s\n", flash_path.c_str());
	else working = image;

	if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	SDL_Window *win = SDL_CreateWindow("Tamagotchi Smart", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 128 * scale, 128 * scale, SDL_WINDOW_RESIZABLE);
	SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : nullptr;
	if (!ren && win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
	if (!win || !ren) { fprintf(stderr, "SDL window: %s\n", SDL_GetError()); return 1; }
	SDL_RenderSetLogicalSize(ren, 128, 128);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB555, SDL_TEXTUREACCESS_STREAMING, 128, 128);

	tsmart::Machine *m = new tsmart::Machine(working);

	// The clock: restored from IMAGE.bin.rtc so the firmware does not see a dead RTC battery at every start.
	const std::string rtc_path = image_path + ".rtc";
	auto save_rtc = [&]() { const auto r = m->rtc_state(); FILE *f = fopen(rtc_path.c_str(), "w"); if (!f) return;
		fprintf(f, "%llu %lld\n", (unsigned long long)r.counter, (long long)time(nullptr)); for (int i = 0; i < 0x60; i++) fprintf(f, "%x ", r.reg[i]); fclose(f); };
	if (!fresh) { if (FILE *f = fopen(rtc_path.c_str(), "r")) { tsmart::Machine::RtcState r; unsigned long long c = 0; long long t = 0; bool ok = fscanf(f, "%llu %lld", &c, &t) == 2;
		for (int i = 0; ok && i < 0x60; i++) { unsigned v = 0; ok = fscanf(f, "%x", &v) == 1; r.reg[i] = (uint16_t)v; } fclose(f);
		if (ok) { r.counter = c; if (realtime && time(nullptr) > t) r.counter += (uint64_t)(time(nullptr) - t) * 32768; m->set_rtc_state(r); } } }

	// The card library: any number of cards, one in the slot at a time.
	int card_no = -1;                                                                 // index into card_paths, -1 = slot empty
	auto card_name = [&]() { if (card_no < 0) return std::string("no card"); std::string n = card_paths[card_no]; size_t p = n.find_last_of("/\\"); return p == std::string::npos ? n : n.substr(p + 1); };
	auto save_card = [&]() { if (card_no >= 0 && m->card.dirty) { if (write_file(card_paths[card_no] + ".card", m->card.mem)) m->card.dirty = false; else fprintf(stderr, "cannot write %s.card\n", card_paths[card_no].c_str()); } };
	auto insert_card = [&](int n) {
		save_card(); m->eject_card(); card_no = -1;
		if (n < 0 || n >= (int)card_paths.size()) return;
		std::vector<uint8_t> data, original;
		if (!read_file(card_paths[n], original) || original.size() < 0x1000 || original.size() > 0x2000000) { fprintf(stderr, "cannot use %s as a card\n", card_paths[n].c_str()); return; }
		if (!read_file(card_paths[n] + ".card", data) || data.size() != original.size()) data = original;
		m->insert_card(data); card_no = n; fprintf(stderr, "card inserted: %s\n", card_paths[n].c_str()); };
	if (!card_paths.empty()) insert_card(0);
	std::deque<std::pair<int, int>> gesture;                                          // scripted swipe: (pad, frames) steps
	auto swipe = [&](int from, int to) { if (gesture.empty()) { gesture.push_back({from, 6}); gesture.push_back({to, 6}); } };
	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
	uint64_t flash_writes_seen = 0; bool mouse_down = false; int mouse_pad = -1; bool key_pad[4] = {}; bool key_button = false, fast = false, quit = false;
	const double frame_ms = 1000.0 / 30.0; uint64_t next_tick = SDL_GetPerformanceCounter(); const double per_ms = SDL_GetPerformanceFrequency() / 1000.0;
	long frames_shown = 0; int shot_no = 0; std::string error;

	auto save_bmp = [&](const std::string &path) {
		SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom((void *)m->lcd, 128, 128, 15, 128 * 2, SDL_PIXELFORMAT_RGB555);
		if (s) { SDL_SaveBMP(s, path.c_str()); SDL_FreeSurface(s); fprintf(stderr, "wrote %s\n", path.c_str()); } };

	while (!quit) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_QUIT: quit = true; break;
			case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEMOTION: {
				if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) mouse_down = true;
				if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) mouse_down = false;
				int x = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, y = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;   // already in 128x128 units
				mouse_pad = (mouse_down && x >= 0 && x < 128 && y >= 0 && y < 128) ? pad_at(x, y) : -1;
				break; }
			case SDL_DROPFILE: { std::string dropped = e.drop.file; SDL_free(e.drop.file); int found = -1;
				for (size_t i = 0; i < card_paths.size(); i++) if (card_paths[i] == dropped) found = (int)i;
				if (found < 0) { card_paths.push_back(dropped); found = (int)card_paths.size() - 1; }
				insert_card(found); break; }
			case SDL_KEYDOWN: case SDL_KEYUP: {
				const bool down = e.type == SDL_KEYDOWN;
				switch (e.key.keysym.sym) {
				case SDLK_SPACE: case SDLK_RETURN: key_button = down; break;
				case SDLK_q: key_pad[2] = down; break;   case SDLK_w: key_pad[1] = down; break;
				case SDLK_a: key_pad[3] = down; break;   case SDLK_s: key_pad[0] = down; break;
				case SDLK_TAB: fast = down; break;
				case SDLK_UP: if (down && !e.key.repeat) swipe(0, 1); break;       case SDLK_DOWN: if (down && !e.key.repeat) swipe(1, 0); break;
				case SDLK_LEFT: if (down && !e.key.repeat) swipe(0, 3); break;     case SDLK_RIGHT: if (down && !e.key.repeat) swipe(3, 0); break;
				case SDLK_c: if (down && !e.key.repeat) insert_card(card_no + 1 < (int)card_paths.size() ? card_no + 1 : -1); break;
				case SDLK_x: if (down && !e.key.repeat) insert_card(-1); break;
				case SDLK_ESCAPE: if (down) quit = true; break;
				case SDLK_F5: if (down && !e.key.repeat) { save_card(); std::vector<uint8_t> keep = m->flash, keep_card = m->card_present ? m->card.mem : std::vector<uint8_t>(); const auto rtc = m->rtc_state();
					delete m; m = new tsmart::Machine(keep); m->set_rtc_state(rtc); if (!keep_card.empty()) m->insert_card(keep_card); flash_writes_seen = 0; error.clear(); } break;
				case SDLK_F12: if (down && !e.key.repeat) { char n[32]; snprintf(n, sizeof n, ".shot%03d.bmp", shot_no++); save_bmp(image_path + n); } break;
				default: break; }
				break; }
			default: break; }
		}
		m->button = key_button;
		int gesture_pad = -1; if (!gesture.empty()) { gesture_pad = gesture.front().first; if (--gesture.front().second <= 0) gesture.pop_front(); }
		for (int p = 0; p < 4; p++) m->pad[p] = key_pad[p] || mouse_pad == p || gesture_pad == p;

		if (error.empty()) {
			try {
				for (int n = fast ? 8 : 1; n > 0; n--) {                 // one emulated frame per host frame, eight when fast-forwarding
					const uint64_t f0 = m->frames; uint64_t guard = 0;
					while (m->frames == f0 && ++guard < 50000000ull) m->step();
				}
			} catch (std::exception &ex) { error = ex.what(); fprintf(stderr, "emulation stopped: %s (pc %06x)\n", error.c_str(), m->cpu.lpc()); }
		}
		// persist the flash whenever the firmware has finished a batch of writes
		const uint64_t writes = m->nor.n_erase + m->nor.n_program;
		if (writes != flash_writes_seen && (m->io[0x050] & 1)) { flash_writes_seen = writes; if (!write_file(flash_path, m->flash)) fprintf(stderr, "cannot write %s\n", flash_path.c_str()); save_rtc(); }
		if (m->card.dirty && !m->card.selected) save_card();

		SDL_UpdateTexture(tex, nullptr, m->lcd, 128 * 2);
		SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, nullptr, nullptr); SDL_RenderPresent(ren);
		if ((++frames_shown % 15) == 0) { char t[400]; snprintf(t, sizeof t, "Tamagotchi Smart  |  %s  |  scene %04x%s%s%s%s", card_name().c_str(), m->ram[0x494], m->sleeping ? "  |  asleep (press the button)" : "", fast ? "  |  fast" : "", error.empty() ? "" : "  |  STOPPED: ", error.c_str()); SDL_SetWindowTitle(win, t); }
		if (exit_after >= 0 && frames_shown >= exit_after) quit = true;

		next_tick += (uint64_t)(frame_ms * per_ms);                       // pace to 30 frames per second
		const uint64_t now = SDL_GetPerformanceCounter();
		if (exit_after >= 0) next_tick = now;                             // tests run unthrottled
		else if (next_tick > now) SDL_Delay((uint32_t)((next_tick - now) / per_ms)); else if (now - next_tick > (uint64_t)(200 * per_ms)) next_tick = now;
	}
	if (!shot_at_exit.empty()) save_bmp(shot_at_exit);
	if (m->nor.n_erase + m->nor.n_program) write_file(flash_path, m->flash);
	save_card(); save_rtc();
	delete m;
	SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
	return 0;
}
