# SPDX-License-Identifier: Apache-2.0
# Host convenience target for the German morphology regression bench.
# The GEOS geode itself is built with mkmf/pmake; this GNUmakefile only
# exposes the host-side bench so `make -C Dictionaries/BuildTools/bench bench`
# works on a Linux host with cc and Python 3.

.PHONY: bench clean
bench:
	python3 run_benchmarks.py

clean:
	rm -rf dicts bench
