"""Parse Westwood CSF string tables (RA2/Yuri's Revenge game text).

CSF layout (little-endian):
  header: " FSC", version(int32), flags(int32), numLabels, numStrings,
          unused, language
  per label: "LBL " + int(0) + name (NUL-terminated)
             per string: " RTS" + int length + ansi bytes (length-1 text
             after a leading colon) [+ optional "WRTS" + int + wide bytes]
"""
