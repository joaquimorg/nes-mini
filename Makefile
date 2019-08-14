obj-m := nes-mini.o
KVERSION := `uname -r`

ifneq (,$(findstring -v7, $(KVERSION)))
CFLAGS_nes-mini.o := -DRPI2
endif

all:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) clean