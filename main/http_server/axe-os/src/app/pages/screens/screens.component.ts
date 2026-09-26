import { Component, Input, OnInit } from '@angular/core';
import { AbstractControl, FormArray, FormBuilder, FormGroup, ValidationErrors, Validators } from '@angular/forms';
import { HttpErrorResponse } from '@angular/common/http';
import { NbToastrService } from '@nebular/theme';
import { TranslateService } from '@ngx-translate/core';
import { catchError, of, switchMap } from 'rxjs';

import { LoadingService } from '../../services/loading.service';
import { SystemService } from '../../services/system.service';
import { OtpAuthService, EnsureOtpResult } from '../../services/otp-auth.service';
import { IScreensSettings } from '../../models/IScreensSettings';

/** Simple integer validator: rejects fractional values (e.g. 10.5). */
export function integerValidator(control: AbstractControl): ValidationErrors | null {
  if (control.value === null || control.value === undefined || control.value === '') {
    return null;
  }
  return Number.isInteger(control.value) ? null : { notInteger: true };
}

/** Rejects values with more than 2 decimal places (the firmware stores the price with 2 decimals). */
export function maxTwoDecimalsValidator(control: AbstractControl): ValidationErrors | null {
  if (control.value === null || control.value === undefined || control.value === '') {
    return null;
  }
  const value = Number(control.value);
  if (Number.isNaN(value)) {
    return null;
  }
  const rounded = Math.round(value * 100) / 100;
  return Math.abs(rounded - value) < 1e-9 ? null : { tooManyDecimals: true };
}

@Component({
  selector: 'app-screens',
  templateUrl: './screens.component.html',
  styleUrls: ['./screens.component.scss']
})
export class ScreensComponent implements OnInit {

  public form!: FormGroup;
  @Input() uri = '';

  public unsupported = false;
  public loadError: string | null = null;

  public minSecs = 3;
  public maxSecs = 600;
  public screensMeta: { id: number; name: string }[] = [];

  public hasPowerBill = false;
  public minPrice = 0;
  public maxPrice = 10;
  public readonly currencySuggestions: string[] = ['R$', '$', '€', '£'];

  private lastLoadedData!: IScreensSettings;

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastrService: NbToastrService,
    private loadingService: LoadingService,
    private translate: TranslateService,
    private otpAuth: OtpAuthService,
  ) { }

  ngOnInit(): void {
    this.systemService.getScreens(this.uri)
      .pipe(
        this.loadingService.lockUIUntilComplete(),
        catchError((err: HttpErrorResponse) => {
          // Firmware without this route: the ESP web server's catch-all GET handler answers
          // unknown extension-less paths with "302 -> /", so the browser follows it and gets
          // index.html (HTTP 200, not JSON) -> JSON parse error. Treat that like a 404.
          const gotHtmlInsteadOfJson = err.status >= 200 && err.status < 300;
          if (err.status === 404 || gotHtmlInsteadOfJson) {
            this.unsupported = true;
          } else {
            this.loadError = this.translate.instant('SCREENS.LOADING_FAILED');
          }
          return of(null);
        })
      )
      .subscribe((data: IScreensSettings | null) => {
        if (data != null && !Array.isArray(data.screens)) {
          // Valid JSON but not the expected contract -> also unsupported.
          this.unsupported = true;
          return;
        }
        if (data != null) {
          this.lastLoadedData = data;
          this.buildForm(data);
        }
      });
  }

  private buildForm(data: IScreensSettings): void {
    this.minSecs = data.minSecs;
    this.maxSecs = data.maxSecs;
    this.screensMeta = data.screens.map(s => ({ id: s.id, name: s.name }));

    this.hasPowerBill = data.powerBill != null;
    if (data.powerBill != null) {
      this.minPrice = data.powerBill.minPrice;
      this.maxPrice = data.powerBill.maxPrice;
    }

    this.form = this.fb.group({
      defaultSecs: [data.defaultSecs, [
        Validators.required,
        Validators.min(data.minSecs),
        Validators.max(data.maxSecs),
        integerValidator,
      ]],
      screens: this.fb.array(data.screens.map(s => this.fb.group({
        enabled: [s.enabled],
        secs: [{ value: s.secs, disabled: !s.enabled }, [
          Validators.required,
          Validators.min(data.minSecs),
          Validators.max(data.maxSecs),
          integerValidator,
        ]],
      }))),
      ...(data.powerBill != null ? {
        powerBill: this.fb.group({
          currency: [data.powerBill.currency, [
            Validators.required,
            Validators.minLength(1),
            Validators.maxLength(4),
          ]],
          pricePerKwh: [data.powerBill.pricePerKwh, [
            Validators.required,
            Validators.min(data.powerBill.minPrice),
            Validators.max(data.powerBill.maxPrice),
            maxTwoDecimalsValidator,
          ]],
        }),
      } : {}),
    });

    this.screensArray.controls.forEach((group) => {
      const enabledControl = (group as FormGroup).get('enabled');
      const secsControl = (group as FormGroup).get('secs');
      enabledControl?.valueChanges.subscribe((enabled: boolean) => {
        if (enabled) {
          secsControl?.enable({ emitEvent: false });
        } else {
          secsControl?.disable({ emitEvent: false });
        }
      });
    });
  }

  public get screensArray(): FormArray {
    return this.form.get('screens') as FormArray;
  }

  public get powerBillGroup(): FormGroup | null {
    return this.form.get('powerBill') as FormGroup | null;
  }

  public setCurrency(value: string): void {
    const currencyControl = this.powerBillGroup?.get('currency');
    if (!currencyControl) {
      return;
    }
    currencyControl.setValue(value);
    currencyControl.markAsDirty();
  }

  public activeCount(): number {
    return this.screensArray.controls.filter(c => c.get('enabled')?.value === true).length;
  }

  public cycleTotalSecs(): number {
    return this.screensArray.getRawValue()
      .filter((s: { enabled: boolean; secs: number }) => s.enabled)
      .reduce((sum: number, s: { enabled: boolean; secs: number }) => sum + (s.secs || 0), 0);
  }

  public cycleMinutes(): number {
    return Math.floor(this.cycleTotalSecs() / 60);
  }

  public cycleSeconds(): number {
    return this.cycleTotalSecs() % 60;
  }

  public hasNoActiveScreens(): boolean {
    return this.activeCount() === 0;
  }

  public pad2(n: number): string {
    return n.toString().padStart(2, '0');
  }

  public applyDefaultToAll(): void {
    const defaultSecsControl = this.form.get('defaultSecs');
    if (!defaultSecsControl || defaultSecsControl.invalid) {
      return;
    }
    const value = defaultSecsControl.value;
    this.screensArray.controls.forEach((group) => {
      if ((group as FormGroup).get('enabled')?.value === true) {
        const secsControl = (group as FormGroup).get('secs');
        secsControl?.setValue(value);
        secsControl?.markAsDirty();
      }
    });
  }

  public save(): void {
    if (!this.form.valid || this.hasNoActiveScreens()) {
      return;
    }

    const raw = this.form.getRawValue();
    const payload: {
      screens?: { id: number; enabled: boolean; secs: number }[];
      defaultSecs?: number;
      powerBill?: { currency: string; pricePerKwh: number };
    } = {};

    if (this.screensArray.dirty || this.form.get('defaultSecs')?.dirty) {
      payload.screens = raw.screens.map((s: { enabled: boolean; secs: number }, i: number) => ({
        id: this.screensMeta[i].id,
        enabled: s.enabled,
        secs: s.secs,
      }));
      payload.defaultSecs = raw.defaultSecs;
    }

    if (this.hasPowerBill && this.powerBillGroup?.dirty) {
      payload.powerBill = {
        currency: raw.powerBill.currency,
        pricePerKwh: raw.powerBill.pricePerKwh,
      };
    }

    if (Object.keys(payload).length === 0) {
      return;
    }

    this.otpAuth.ensureOtp$(
      this.uri,
      this.translate.instant('SECURITY.OTP_TITLE'),
      this.translate.instant('SECURITY.OTP_HINT')
    )
      .pipe(
        switchMap(({ totp }: EnsureOtpResult) =>
          this.systemService.updateScreens(this.uri, payload, totp)
            .pipe(this.loadingService.lockUIUntilComplete())
        ),
      )
      .subscribe({
        next: (response: IScreensSettings) => {
          this.lastLoadedData = response;
          this.buildForm(response);
          this.toastrService.success(this.translate.instant('SCREENS.SETTINGS_SAVED'), this.translate.instant('COMMON.SUCCESS'));
        },
        error: (err: HttpErrorResponse) => {
          const message = typeof err.error === 'string' ? err.error : err.message;
          this.toastrService.danger(message, this.translate.instant('SCREENS.SETTINGS_SAVE_FAILED'));
        }
      });
  }

  public discard(): void {
    this.buildForm(this.lastLoadedData);
  }
}
