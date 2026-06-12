#!/bin/sh

BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")

# --tags: looks for any tag
# --always: if no tag is found, fallback to the commit hash
# --dirty: appends "-dirty" automatically if there are uncommitted changes
DESCRIBE=$(git describe --tags --always --dirty 2>/dev/null || echo "unknown")

COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")

# This checks if the string ends with "-dirty"
if echo "$DESCRIBE" | grep -q "dirty"; then
    IS_DIRTY=1
else
    IS_DIRTY=0
fi

cat <<EOF > version.h.tmp
#ifndef VERSION_H
#define VERSION_H

#define GIT_BRANCH  "$BRANCH"
#define GIT_TAG     "$DESCRIBE"
#define GIT_COMMIT  "$COMMIT"
#define GIT_IS_DIRTY $IS_DIRTY

#endif
EOF

# Update version.h only if content changed to avoid massive re-compiles
if ! cmp -s version.h.tmp version.h; then
    mv version.h.tmp version.h
else
    rm version.h.tmp
fi
