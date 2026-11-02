# XAM launcher palette refusal and failed muzzle-axis experiment

User test of 41b141374b4e86fe98fdf71a49f9b1d135180553:
standard assault rifle normal; Stinger and RPG-7 still head-locked; MP5 points
backward; UI Uzi/MP7 lies sideways and points left. ODST dual wield accepted
in this report (scoped acceptance, not a new release baseline).

Input log: attachment file_000000001664820ebe33847fbe83d42f,
/workspace/attachments/5872283e-5f88-4325-8800-e5883060085f/HaloMCCVR.log.
At 12:47:55 through 12:48:07, graph EE110C9D then EBD20A5E, identity
4C86902A3289986E/count42, stage3, invalidNodes F81F01F2A. Left segment
lengths zero; right lengths approximately .102584/.108863. The cache graph
identifies every rejected index as a left shoulder/elbow/wrist/finger node.
Gun and right-side matrices pass the existing validity checks.

The log does NOT record scale or prove why the left matrices are invalid.
The new path is therefore conditional: exact graph/count/roles, complete
left mask, all hidden nodes finite with exactly zero scale, all other nodes
valid. If this condition is not present, the normal refusal is preserved and
an added zeroScale mask provides the missing evidence. No broad relaxation
of matrix validation, synthetic visible left hand or native binding change.

The right-only transaction carries the authored right wrist, gun subtree and
right fingers to the primary controller, retains right-arm IK and floating
arm policy, and preserves omitted left matrices exactly. Hidden source
geometry remains hidden. The special path is confined to the shared XAM
launcher graph and cannot admit unrelated standard graphs.

The marker-frame experiment is disabled, not deleted, in a separate commit.
Its self-consistent axis tests did not establish visible barrel alignment;
the user's result disproves that assumption. MP5/MP7 orientation is unresolved.
Do not describe this candidate as correcting either imported gun's rotation.

Portable tests cover launcher carry and component offsets, hidden-node byte
preservation, rejection without output mutation, malformed live gun, nonfinite
hidden node, partially visible left arm and unrelated identity. Full existing
CE suite passes GCC ASan/UBSan with the previously documented Linux shim.
Windows compilation and headset confirmation remain pending. Next test: normal
AR, Stinger, RPG and reload/support-grip; preserve accepted ODST behavior.
