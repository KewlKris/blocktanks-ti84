# ----------------------------
# Makefile Options
# ----------------------------

NAME = BTANKS
ICON = icon.png
DESCRIPTION = "BlockTanks.io in Ti-84 CE"
COMPRESSED = YES
ARCHIVED = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
