CFLAGS ?= -O2 -g -std=c89 -pedantic -Wall -Wextra

CFLAGS += $(shell pkg-config --cflags fuse3)
LDLIBS += $(shell pkg-config --libs fuse3)
CPPFLAGS += -DFUSE_USE_VERSION=35

CFLAGS += -pthread

CFLAGS += \
          -Weverything \
          -Werror=format \
          -Werror=fortify-source \
          -Werror=implicit-function-declaration \
          -Werror=incompatible-function-pointer-types \
          -Werror=int-conversion \
          -Werror=return-type \
          -Werror=sometimes-uninitialized \
          -Werror=uninitialized \
          -Wno-atomic-implicit-seq-cst \
          -Wno-c++98-compat \
          -Wno-disabled-macro-expansion \
          -Wno-documentation \
          -Wno-format-nonliteral \
          -Wno-gnu-auto-type \
          -Wno-gnu-conditional-omitted-operand \
          -Wno-gnu-statement-expression \
          -Wno-gnu-zero-variadic-macro-arguments \
          -Wno-language-extension-token \
          -Wno-padded \
          -Wno-reserved-id-macro \
          -Wframe-larger-than=256 \
# .

unjumble: unjumble.c
	clang $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

install:
	@ln -fsv "$$PWD/unjumble" ~/.local/bin/unjumble
