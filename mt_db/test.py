#!/usr/bin/python

import subprocess
import sys

def verify_server(server):
  times_to_run = 10
  print ""
  sys.stdout.write("Testing " + server)
  sys.stdout.flush()
  for i in range(times_to_run):
    p = subprocess.Popen("./" + server + " < test/250_add_remove", shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    expected_not_found = 307
    actual_not_found = 0
    for l in p.stdout.readlines():
      if l == "not found\n":
        actual_not_found += 1
    p.wait()
    if actual_not_found != expected_not_found:
      sys.stdout.write("FAILED")
      sys.stdout.flush()
      return
    sys.stdout.write(".")
    sys.stdout.flush()
  sys.stdout.write("PASSED")
  sys.stdout.flush()
  return


def main():
  servers = ["server_coarse", "server_rw", "server_fine"]
  for server in servers:
    verify_server(server)
  print ""

  
if __name__ == "__main__":
  main()

