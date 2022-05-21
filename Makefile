SOURCES	:= pchtxt
TARGET	:= pchtxt2ips
BUILD_DIR	:= build
PROGRAM_DIR	:= .

CFLAGS	:= -O3 -Wall
CXXFLAGS := -std=c++20

CFILES		:=	$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(wildcard $(dir)/*.cpp))
OFILES		:=	$(CFILES:.c=.o) $(CPPFILES:.cpp=.o)
BUILT_OBJECTS	:=	$(foreach object,$(OFILES),$(BUILD_DIR)/$(notdir $(object)))

%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) -Iinclude $(CFLAGS) $(INCLUDES) -c $< -o $(BUILD_DIR)/$(notdir $@)

%.o: %.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) -Iinclude $(CFLAGS) $(CXXFLAGS) $(INCLUDES) -c $< -o $(BUILD_DIR)/$(notdir $@)

all: pchtxt/$(OFILES)
	$(CXX) main.cpp -Iinclude $(CFLAGS) $(CXXFLAGS) $(INCLUDES) -o $(PROGRAM_DIR)/$(TARGET) $(BUILT_OBJECTS)

clean: 
	rm -f $(TARGET)
	rm -rf $(BUILD_DIR)
# rm -f build/*.o
