import subprocess

lines = [l for l in open("build-ng/build.ninja") if "m68krecomp --raw" in l]
line = lines[0]
cmd = line.split("COMMAND = ", 1)[1].strip().strip('"')
cmd = cmd.split(" && ", 1)[1]
print("cmd length:", len(cmd))
r = subprocess.run(cmd, shell=True, capture_output=True, text=True, cwd="build-ng")
print("rc:", r.returncode)
print("stdout:", r.stdout[:500])
print("stderr:", r.stderr[:500])
