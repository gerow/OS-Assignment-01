#!/usr/bin/python

import subprocess
import sys

def verify_server(server):
  times_to_run = 5
  for i in range(times_to_run):
    p = subprocess.Popen("./" + server + " < test/250_add_remove", shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    expected_not_found = 307
    actual_not_found = 0
    for l in p.stdout.readlines():
      if l == "not found\n":
        actual_not_found += 1
    p.wait()
    if actual_not_found != expected_not_found:
      print server + ": FAILED"
      return
  print server + ": PASSED"
  return


def main():
  servers = ["server_coarse", "server_rw", "server_fine"]
  for server in servers:
    verify_server(server)

  
if __name__ == "__main__":
  main()

