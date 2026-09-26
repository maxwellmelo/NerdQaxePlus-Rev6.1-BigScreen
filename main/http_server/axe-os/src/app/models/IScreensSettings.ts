export interface IScreenItem {
  id: number;
  name: string;
  enabled: boolean;
  secs: number;
}

export interface IPowerBill {
  currency: string;
  pricePerKwh: number;
  minPrice: number;
  maxPrice: number;
}

export interface IScreensSettings {
  defaultSecs: number;
  minSecs: number;
  maxSecs: number;
  screens: IScreenItem[];
  /** Absent on firmware builds that do not yet support electricity cost tracking. */
  powerBill?: IPowerBill;
}
