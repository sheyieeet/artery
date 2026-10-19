package artery.inet;

// Corrected link-layer PRR. Legacy CPM distance observations remain unchanged.
// Outputs are aggregate scalars: no packets or per-packet vectors are retained.
simple CpmLinkPrrObserver
{
    parameters:
        @display("i=block/table");
}
