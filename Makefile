SUBPROJS=echo

.PHONY: clean $(SUBPROJS) all

all: $(SUBPROJS)

$(SUBPROJS): 
	$(MAKE) -C "$@" all

clean:
	@for i in $(SUBPROJS); do \
		$(MAKE) -C $$i clean; \
	done
