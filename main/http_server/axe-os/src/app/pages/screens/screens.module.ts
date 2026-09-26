import { NgModule } from '@angular/core';
import { ReactiveFormsModule, FormsModule } from '@angular/forms';
import { CommonModule } from '@angular/common';
import { NbCardModule, NbButtonModule, NbInputModule, NbToggleModule } from '@nebular/theme';
import { TranslateModule } from '@ngx-translate/core';
import { ScreensComponent } from './screens.component';

@NgModule({
  declarations: [
    ScreensComponent
  ],
  imports: [
    CommonModule,
    ReactiveFormsModule,
    FormsModule,
    NbCardModule,
    NbButtonModule,
    NbInputModule,
    NbToggleModule,
    TranslateModule
  ],
  exports: [
    ScreensComponent
  ]
})
export class ScreensModule { }
