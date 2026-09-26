<#
.SYNOPSIS
    Gera os .c de fonte bitmap LVGL v8 (4bpp) para as 3 telas-piloto do
    minerador, a partir dos TTFs baixados/instanciados.

.DESCRIPTION
    Roda `npx lv_font_conv` (pacote local em node_modules/, ver package.json)
    uma vez por combinacao familia/peso/tamanho/glifos. Os .c saem direto em
    main/displays/fonts/ (path relativo), no mesmo formato ja usado no
    projeto para outras fontes (ver main/displays/images/ui_font_vt323_21.c,
    que ja usa exatamente esse formato de saida do lv_font_conv e ja compila
    no projeto) - por isso NAO usamos --lv-include: o padrao do lv_font_conv
    (#ifdef LV_LVGL_H_INCLUDE_SIMPLE / #include "lvgl.h" else "lvgl/lvgl.h")
    ja e o padrao usado e funcional neste repo.

    Os conjuntos de glifos usam --range com codepoints hex (nao --symbols
    com caracteres acentuados literais) para nao depender da code page do
    PowerShell/console ao repassar argumentos ao processo node filho:

      NUM  = digitos 0-9, vírgula, ponto, hifen, porcento, grau
             (0x25,0x2C-0x2E,0x30-0x39,0xB0)
      FULL = ASCII imprimivel (0x20-0x7E) + Latin-1 pt-BR
             (a-á-â-ã-ç-é-ê-í-ó-ô-õ-ú maiusculas/minusculas) + grau

    Cada combinacao de peso/tamanho/glifo abaixo foi decidida lendo o CSS
    real do design aprovado para as 3 telas-piloto (19-zen, 01-painel,
    14-hashrate), nao por presuncao - revise com cuidado antes de mudar.

.NOTES
    Pre-requisitos: `npm install` dentro desta pasta (lv_font_conv local) e
    `python tools/fonts/instance-archivo.py` (gera os TTFs estaticos do
    Archivo a partir do variable font).
#>

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$outDir = Join-Path $root '..\..\main\displays\fonts'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# ranges de glifos (ver cabecalho do arquivo)
$NUM = '0x25,0x2C-0x2E,0x30-0x39,0xB0'
$FULL = '0x20-0x7E,0xB0,0xC0-0xC3,0xC7,0xC9,0xCA,0xCD,0xD3-0xD5,0xDA,0xE0-0xE3,0xE7,0xE9,0xEA,0xED,0xF3-0xF5,0xFA'

$jobs = @(
    # --- 19-zen: numero-hero -------------------------------------------------
    @{ Name = 'font_instrument_serif_hero';        Font = 'ttf\InstrumentSerif-Regular.ttf';        Size = 188; Range = $NUM }

    # --- 01-painel: Saira Condensed ------------------------------------------
    @{ Name = 'font_saira_condensed_bold_108';      Font = 'ttf\SairaCondensed-Bold.ttf';            Size = 108; Range = $NUM }
    @{ Name = 'font_saira_condensed_bold_28';       Font = 'ttf\SairaCondensed-Bold.ttf';            Size = 28;  Range = $NUM }
    @{ Name = 'font_saira_condensed_semibold_24';   Font = 'ttf\SairaCondensed-SemiBold.ttf';        Size = 24;  Range = $FULL }
    @{ Name = 'font_saira_condensed_semibold_15';   Font = 'ttf\SairaCondensed-SemiBold.ttf';        Size = 15;  Range = $NUM }

    # --- Archivo: textos gerais + 14-hashrate --------------------------------
    @{ Name = 'font_archivo_regular_15';            Font = 'instanced\Archivo-Regular-wdth88.ttf';   Size = 15;  Range = $FULL }
    @{ Name = 'font_archivo_regular_19';            Font = 'instanced\Archivo-Regular-wdth88.ttf';   Size = 19;  Range = $FULL }
    @{ Name = 'font_archivo_medium_15';             Font = 'instanced\Archivo-Medium-wdth82.ttf';    Size = 15;  Range = $FULL }
    @{ Name = 'font_archivo_medium_17';             Font = 'instanced\Archivo-Medium-wdth88.ttf';    Size = 17;  Range = $FULL }
    @{ Name = 'font_archivo_bold_20';               Font = 'instanced\Archivo-Bold-wdth100.ttf';     Size = 20;  Range = $FULL }
    @{ Name = 'font_archivo_bold_24';               Font = 'instanced\Archivo-Bold-wdth100.ttf';     Size = 24;  Range = $NUM }
)

foreach ($j in $jobs) {
    $out = Join-Path $outDir ($j.Name + '.c')
    Write-Host "Gerando $($j.Name) ($($j.Size)px) <- $($j.Font) ..."
    npx lv_font_conv `
        --font $j.Font `
        -r $j.Range `
        --size $j.Size `
        --bpp 4 `
        --format lvgl `
        --lv-font-name $j.Name `
        -o $out
    if ($LASTEXITCODE -ne 0) {
        throw "lv_font_conv falhou para $($j.Name) (exit $LASTEXITCODE)"
    }
}

Write-Host "`n$($jobs.Count) fontes geradas em $outDir"
