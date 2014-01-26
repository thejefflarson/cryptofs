all:
	python waf
clean:
	python waf clean
test: all
	prove -r ./test
PHONY: all clean test

