# CamClops Dashcam Explorer Makefile
# SN: 00020

CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2
LDFLAGS = -lstdc++fs

# Extract version components
V_MAJ = $(shell grep "define VERSION_MAJOR" version.hpp | awk '{print $$3}')
V_MIN = $(shell grep "define VERSION_MINOR" version.hpp | awk '{print $$3}')
V_PAT = $(shell grep "define VERSION_PATCH" version.hpp | awk '{print $$3}')
V_SUF = $(shell grep "define VERSION_SUFFIX" version.hpp | awk '{print $$3}' | tr -d '"')
VERSION = $(V_MAJ).$(V_MIN).$(V_PAT)$(V_SUF)

TARGET = camclops
SRC = main.cpp trip_detection.cpp config_manager.cpp find_trips.cpp \
      gpx_export.cpp prefs.cpp kml_prefs.cpp locations.cpp video_build.cpp
OBJ = $(SRC:.cpp=.o)

# Static SN Audit File
SN_FILE = sn_audit.txt

.PHONY: all clean banner sn-audit archive

all: banner $(TARGET)

banner:
	@echo "********************************************"
	@echo "* *"
	@echo "* Building CamClops version $(VERSION)       *"
	@echo "* *"
	@echo "********************************************"

# Audit Serial Numbers into a static file
sn-audit:
	@echo "--- CamClops SN Audit: $(shell date '+%Y-%m-%d %H:%M:%S') ---" > $(SN_FILE)
	@grep -H "^// SN: " *.cpp *.hpp camclops_project_brief.md | awk -F':// SN: ' '{printf "%-35s %s\n", $$1, $$2}' >> $(SN_FILE) 2>/dev/null || true
	@grep -H "^# SN: " Makefile | awk -F':# SN: ' '{printf "%-35s %s\n", $$1, $$2}' >> $(SN_FILE) 2>/dev/null || true
	@echo "SN Audit updated in $(SN_FILE)"

$(TARGET): sn-audit $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Archive logic: Includes sn_audit.txt in the root of the tar ball
archive: sn-audit
	@mkdir -p archive
	tar -cf archive/$(VERSION).tar *.cpp *.hpp Makefile CHANGELOG.md $(SN_FILE)
	@echo "Source and $(SN_FILE) archived to archive/$(VERSION).tar"

clean:
	rm -f $(OBJ) $(TARGET) $(SN_FILE)
	@echo "Objects, binary, and audit log cleared."

clean-logs:
	rm -rf archive/*.tar
	@echo "Old archives cleared."
