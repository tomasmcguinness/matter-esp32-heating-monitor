// The subscription state of one of the home's configured sensors, as reported by build_home_json.
// "none" means nothing is configured -- the section shows its own alert for that -- so it renders
// nothing, as does a state not yet loaded.
//
const states: Record<string, { variant: string, label: string }> = {
    subscribed: { variant: "success", label: "Subscribed" },
    pending: { variant: "warning", label: "Subscribing" },
    unsubscribed: { variant: "danger", label: "Not subscribed" },
    missing: { variant: "danger", label: "Device missing" },
};

function SubscriptionPill({ state }: { state: string | undefined }) {

    const pill = state ? states[state] : undefined;

    if (!pill) {
        return null;
    }

    // Bootstrap's subtle palette, at a fixed small size so it doesn't scale up with the heading it sits in.
    const className = `badge rounded-pill fw-normal border bg-${pill.variant}-subtle text-${pill.variant}-emphasis border-${pill.variant}-subtle`;

    return <span className={className} style={{ fontSize: '0.7rem' }}>{pill.label}</span>;
}

export default SubscriptionPill;
