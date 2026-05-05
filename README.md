# OpenJ9FootprintAnalysis
Tool for finding what consumes memory (RSS) in OpenJ9

To compile:
	make

Manual analysis:
	./footprintAnalysis.linux -s smapsFile -j javacoreFile -c callsitesFile

Automated collection and analysis:
	python3 collect_openj9_footprint.py PID

The Python collector:
- copies `/proc/PID/smaps` first
- copies `/proc/PID/maps`, `/proc/PID/status`, and `/proc/PID/cmdline`
- sends `SIGQUIT` to the JVM
- waits for new javacore and core files for that PID to appear in `/tmp`
- resolves `jdmpview` from the same Java installation as the target JVM
- runs `jdmpview -core <core> '!printallcallsites'`
- runs `./footprintAnalysis.linux -s <smaps> -j <javacore> -c <callsites>`

Example:
	./collect_openj9_footprint.py 12345

Optional arguments:
	--dump-dir /tmp
	--output-dir .
	--footprint-binary ./footprintAnalysis.linux
	--wait-timeout 300
	--stable-seconds 3
	-v               show more tracing
	-vv              show even more tracing
	-q               only print warnings/errors to console

Tracing:
- all activity is always written to `collector.log`
- default console mode prints high-level progress
- `-v` also prints executed external commands
- `-q` keeps console output minimal while preserving the full log

Output:
A session directory is created with the PID in the artifact filenames, containing:
- copied proc files
- copied javacore and core
- extracted callsites
- footprint analysis output
- stderr/stdout captures
- manifest.json
- collector.log
