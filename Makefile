PREFIX ?= /usr/local

CXXFLAGS ?= -std=c++17 -O2 -Wall

pomo: pomo.cpp platform_linux.cpp platform.h
	$(CXX) $(CXXFLAGS) -o $@ pomo.cpp platform_linux.cpp

# Windows build with MinGW-w64, either cross-compiling from Linux or natively.
# -static so pomo.exe runs on its own, without MinGW's DLLs.
WINDOWS_CXX ?= x86_64-w64-mingw32-g++
pomo.exe: pomo.cpp platform_windows.cpp platform.h
	$(WINDOWS_CXX) $(CXXFLAGS) -static -o $@ pomo.cpp platform_windows.cpp -lwinmm

install: pomo
	install -Dm755 pomo $(DESTDIR)$(PREFIX)/bin/pomo
	install -Dm644 pomo.1 $(DESTDIR)$(PREFIX)/share/man/man1/pomo.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/pomo $(DESTDIR)$(PREFIX)/share/man/man1/pomo.1

clean:
	rm -f pomo pomo.exe

# GNOME Shell helper so -m can really minimize/restore the window (log out and back in after the first install)
EXT = $(HOME)/.local/share/gnome-shell/extensions/pomo@ajchurchill
install-extension:
	install -Dm644 -t $(EXT) extension/metadata.json extension/extension.js
	gnome-extensions enable pomo@ajchurchill || echo "Log out and back in, then run: gnome-extensions enable pomo@ajchurchill"

uninstall-extension:
	-gnome-extensions disable pomo@ajchurchill
	rm -rf $(EXT)
