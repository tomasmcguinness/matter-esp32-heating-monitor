// COP as the firmware sends it: hundredths, so 325 is 3.25. Null while the heat source is idle or
// either meter is missing, which renders as a dash rather than a misleading zero.
function Cop({ children }: { children: number | null | undefined }) {

    if (children === null || children === undefined || isNaN(children)) {
        return <span>-</span>;
    }

    return <span>{(children / 100.0).toFixed(2)}</span>;
}

export default Cop;
