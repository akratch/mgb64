# Escalations Log

The loop's stop-instead-of-guess record (charter rule 10 and Appendix B).
An entry is appended here when:

- the same finding fails verify twice (it drops out of the actionable list),
- evidence is contradictory and acting would mean guessing, or
- the required action is irreversible (deleting baselines, force-push,
  remote push) and therefore owner-only.

Escalated findings stay in the ledger with their last honest status; the
entry here names the finding, the contradiction or blocker, and what an
owner/controller must decide to unblock it.

Format, one entry per escalation:

```
## YYYY-MM-DD FID-NNNN — short title
- Lane/phase:
- What was attempted (evidence paths):
- The contradiction or irreversible action:
- Decision needed from owner/controller:
- Resolution (filled when closed):
```

_No open escalations._
