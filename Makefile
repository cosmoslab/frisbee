SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
MAKEFLAGS += --warn-undefined-variables

OUTPUT_DIR ?= outputs

IMAGEZIP_DIR := src/imagezip
IMAGEZIP_BIN := imagezip imageunzip imagedump
IMAGEZIP_TGT := $(addprefix $(IMAGEZIP_DIR)/, $(IMAGEZIP_BIN))
IMAGEZIP_OUT := $(addprefix $(OUTPUT_DIR)/, $(IMAGEZIP_BIN))

FRISBEE_DIR := src/frisbee.redux
FRISBEE_BIN := frisbee frisbeed frisupload
FRISBEE_TGT := $(addprefix $(FRISBEE_DIR)/, $(FRISBEE_BIN))
FRISBEE_OUT := $(addprefix $(OUTPUT_DIR)/, $(FRISBEE_BIN))

TARGETS := $(IMAGEZIP_TGT) $(FRISBEE_TGT)
OUTPUTS := $(IMAGEZIP_OUT) $(FRISBEE_OUT)

.PHONY: build
build: $(TARGETS)

$(TARGETS):
	#change to src directory, then run the makefile
	$(MAKE) -C $(@D) -f Makefile-linux.sa $(@F)
	#ensure output dir exists
	mkdir -p $(OUTPUT_DIR)
	#copy target to output directory
	cp $(@) $(OUTPUT_DIR)/$(@F)


.PHONY: install
install: $(OUTPUTS)

#prereqs for each output file
$(IMAGEZIP_OUT): $(IMAGEZIP_TGT)
$(FRISBEE_OUT): $(FRISBEE_TGT)

$(OUTPUTS):
	#ask target bin to print its version 
	#so we know its in the right place and working
	#this can be used for annotations
	# $(@)

.PHONY: all
all: $(OUTPUT_DIR).tgz

$(OUTPUT_DIR).tgz: install
	#tar up the output dir
	tar -czvf $(OUTPUT_DIR).tgz $(OUTPUT_DIR)
