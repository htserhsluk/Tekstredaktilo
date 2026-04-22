# Tekstredaktilo

A CLI-based collaborative text editor written entirely in C. This project demonstrates advanced OS principles by allowing multiple users to connect to a central server and edit a shared document simultaneously in real-time from their terminals.

At its core, the editor tackles the complex problem of concurrent text modification using a custom hybrid OT/CRDT (Operational Transformation / Conflict-free Replicated Data Type) engine. This ensures that even when multiple clients type at the exact same millisecond, the document state remains perfectly synchronized across all peers without data corruption.
