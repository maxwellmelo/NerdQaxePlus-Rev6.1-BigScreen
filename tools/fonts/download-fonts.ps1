<#
.SYNOPSIS
    Baixa os TTFs (licenca OFL) das 3 familias usadas nas telas-piloto do
    minerador, direto da pasta ofl/<familia> do repo github.com/google/fonts.

.DESCRIPTION
    Os nomes de arquivo foram descobertos via
    https://api.github.com/repos/google/fonts/contents/ofl/<familia>
    (nunca adivinhados) em 2026-09-22:

      - ofl/instrumentserif  -> so tem Regular e Italic estaticos (confirma
        que a familia realmente so tem peso 400 no Google Fonts).
      - ofl/sairacondensed   -> tem estaticos por peso (Regular, Medium,
        SemiBold, Bold, Black, ExtraBold, ExtraLight, Light, Thin). NAO e
        variable font nesse repo, ao contrario do que a tarefa avisava como
        possibilidade -> nao precisa de fonttools instancer para essa familia.
      - ofl/archivo          -> E variable font (eixo wght 100-900, eixo
        wdth 62-125), um unico arquivo "Archivo[wdth,wght].ttf" (+ Italic).
        Precisa de instanciacao (ver instance-archivo.ps1) para virar TTFs
        estaticos antes do lv_font_conv.

.NOTES
    So acessa github.com (rede publica), nunca 192.168.x.x.
#>

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $root 'ttf'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$files = @(
    # Instrument Serif - so peso Regular (400) existe no Google Fonts
    @{ Url = 'https://raw.githubusercontent.com/google/fonts/main/ofl/instrumentserif/InstrumentSerif-Regular.ttf'; Out = 'InstrumentSerif-Regular.ttf' }

    # Saira Condensed - estaticos por peso, usamos SemiBold (600) e Bold (700)
    @{ Url = 'https://raw.githubusercontent.com/google/fonts/main/ofl/sairacondensed/SairaCondensed-SemiBold.ttf'; Out = 'SairaCondensed-SemiBold.ttf' }
    @{ Url = 'https://raw.githubusercontent.com/google/fonts/main/ofl/sairacondensed/SairaCondensed-Bold.ttf'; Out = 'SairaCondensed-Bold.ttf' }

    # Archivo - variable font (wght+wdth num unico arquivo); instanciado depois
    @{ Url = 'https://raw.githubusercontent.com/google/fonts/main/ofl/archivo/Archivo%5Bwdth%2Cwght%5D.ttf'; Out = 'Archivo-VF.ttf' }
)

foreach ($f in $files) {
    $dest = Join-Path $outDir $f.Out
    Write-Host "Baixando $($f.Out) ..."
    Invoke-WebRequest -Uri $f.Url -OutFile $dest -UseBasicParsing
    $size = (Get-Item $dest).Length
    Write-Host "  OK - $size bytes"
}

Write-Host "`nTodos os TTFs baixados em $outDir"
