# l.py

Cross-platform directory lister, ls/dir-like with fd-style usability.

## Language

**Target**:
A command-line argument naming what to list. If it ends in a slash, it is always a directory. If it is an existing directory, its entries are listed. If it is an existing file, that file is listed. Otherwise it is a wildcard pattern matched against entry names. No Target means the current directory.
_Avoid_: path, pattern (each covers only part of the meaning)

**Entry**:
One file, directory, or link shown as one line in a listing.

**Link**:
An Entry that points at another name. Shown as `name -> target`, never followed.

**Flat listing**:
Recursive output as one list of Entries with relative paths, sorted across the whole tree.
_Avoid_: tree

**Grouped listing**:
Recursive output with a header per directory and Entries listed under it.
_Avoid_: tree

**Summary**:
The footer with file count, directory count, total size, and free space.

**Lossy name**:
An Entry name that had to be altered to be printed, so it cannot be reliably copied and pasted.
