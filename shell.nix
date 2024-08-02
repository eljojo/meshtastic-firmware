{ pkgs ? import <nixpkgs> {} }:

let
  lib = pkgs.lib;
in
pkgs.mkShell {
  buildInputs = [
    pkgs.platformio
    pkgs.nanopb
    pkgs.esptool
    pkgs.protobuf
  ];
}
