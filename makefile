# file      : makefile
# author    : Boris Kolpackov <boris@codesynthesis.com>
# copyright : Copyright (c) 2005-2010 Code Synthesis Tools CC
# license   : GNU GPL v2; see accompanying LICENSE file

include $(dir $(lastword $(MAKEFILE_LIST)))build/bootstrap.make

dirs := odb

default  := $(out_base)/
dist     := $(out_base)/.dist
clean    := $(out_base)/.clean
cleandoc := $(out_base)/.cleandoc

$(default): $(addprefix $(out_base)/,$(addsuffix /,$(dirs)))

$(dist): export dirs := $(dirs)
$(dist): export docs := GPLv2 LICENSE README version
$(dist): data_dist := 
$(dist): exec_dist := bootstrap
$(dist): export extra_dist := $(data_dist) $(exec_dist)
$(dist): export version = $(shell cat $(src_root)/version)

$(dist): $(addprefix $(out_base)/,$(addsuffix /.dist,$(dirs)))
	$(call dist-data,$(docs) $(data_dist))
	$(call dist-exec,$(exec_dist))
	$(call dist-dir,m4)
	$(call meta-automake)
	$(call meta-autoconf)

$(clean): $(addprefix $(out_base)/,$(addsuffix /.clean,$(dirs)))
$(cleandoc): $(out_base)/doc/.cleandoc

$(call include,$(bld_root)/dist.make)
$(call include,$(bld_root)/meta/automake.make)
$(call include,$(bld_root)/meta/autoconf.make)

$(foreach d,$(dirs),$(call import,$(src_base)/$d/makefile))
