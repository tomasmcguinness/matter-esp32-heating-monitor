// The Commodity Price the firmware reports. `price` is per kWh or kVAh, in the currency's major unit
// scaled by 10^decimalPoints, so GBP with 5 decimal points stores 24.567p as 24567. Currency is the
// ISO 4217 numeric code.
//
export type CommodityPrice = {
    price: number;
    decimalPoints: number;
    currency: number;
    tariffUnit: number | null;
};

const symbols: Record<number, string> = {
    826: "£",
    840: "$",
    978: "€",
};

type HourlyCostProps = {
    electricalPower: number | null | undefined; // mW
    commodityPrice: CommodityPrice | null | undefined;
};

// What the house is costing to run right now: this moment's power at this moment's price, so it
// falls to standby pence between runs. A kVAh tariff is treated as kWh, which is one reason this is
// shown as approximate. Worked out here rather than on the device, like every derived figure that
// needs no history.
//
function HourlyCost({ electricalPower, commodityPrice }: HourlyCostProps) {

    if (electricalPower === null || electricalPower === undefined || isNaN(electricalPower) || !commodityPrice) {
        return <span>-</span>;
    }

    const kilowatts = electricalPower / 1000000.0;
    const pricePerKwh = commodityPrice.price / Math.pow(10, commodityPrice.decimalPoints);
    const cost = kilowatts * pricePerKwh;

    const symbol = symbols[commodityPrice.currency] ?? "";
    const amount = Math.abs(cost).toFixed(2);

    return <span>{cost < 0 ? "-" : ""}{symbol}{amount}/h</span>;
}

export default HourlyCost;
