
-include config.mk

CXXFLAGS += -MMD -MP -Ilib
LIBS += $(AOSSDKLIBS)

EXE = aos_floating_neutral_app

SRCS = $(wildcard *.cpp)
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)

all: $(EXE)

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LIBS)

-include $(DEPS)

clean:
	rm -f $(EXE) $(OBJS) $(DEPS) *.aos

.PHONY: all clean

