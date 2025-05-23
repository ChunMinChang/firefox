#!/usr/bin/env python3
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
This utility takes a series of https://crt.sh/ identifiers and writes to
stdout all of those certs' distinguished name or SPKI fields in hex, with an
array of all those. You'll need to post-process this list to handle any
duplicates.

Requires Python 3.
"""
import argparse
import io
import re
import sys

import requests
from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes
from cryptography.x509.oid import NameOID
from pyasn1.codec.der import decoder, encoder
from pyasn1_modules import pem, rfc5280

assert sys.version_info >= (3, 2), "Requires Python 3.2 or later"


def hex_string_for_struct(bytes):
    return [f"0x{x:02X}" for x in bytes]


def hex_string_human_readable(bytes):
    return [f"{x:02X}" for x in bytes]


def nameOIDtoString(oid):
    if oid == NameOID.COUNTRY_NAME:
        return "C"
    if oid == NameOID.COMMON_NAME:
        return "CN"
    if oid == NameOID.LOCALITY_NAME:
        return "L"
    if oid == NameOID.ORGANIZATION_NAME:
        return "O"
    if oid == NameOID.ORGANIZATIONAL_UNIT_NAME:
        return "OU"
    raise Exception(f"Unknown OID: {oid}")


def print_block(pemData, identifierType="DN", crtshId=None):
    substrate = pem.readPemFromFile(io.StringIO(pemData.decode("utf-8")))
    cert, _ = decoder.decode(substrate, asn1Spec=rfc5280.Certificate())
    octets = None

    if identifierType == "DN":
        der_subject = encoder.encode(cert["tbsCertificate"]["subject"])
        octets = hex_string_for_struct(der_subject)
    elif identifierType == "SPKI":
        der_spki = encoder.encode(cert["tbsCertificate"]["subjectPublicKeyInfo"])
        octets = hex_string_for_struct(der_spki)
    else:
        raise Exception("Unknown identifier type: " + identifierType)

    cert = x509.load_pem_x509_certificate(pemData, default_backend())
    common_name = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)[0]
    block_name = "CA{}{}".format(
        re.sub(r"[-:=_. ]", "", common_name.value), identifierType
    )

    fingerprint = hex_string_human_readable(cert.fingerprint(hashes.SHA256()))

    dn_parts = [f"/{nameOIDtoString(part.oid)}={part.value}" for part in cert.subject]
    distinguished_name = "".join(dn_parts)

    print(f"// {distinguished_name}")
    print("// SHA256 Fingerprint: " + ":".join(fingerprint[:16]))
    print("//                     " + ":".join(fingerprint[16:]))
    if crtshId:
        print(f"// https://crt.sh/?id={crtshId} (crt.sh ID={crtshId})")
    print(f"static const uint8_t {block_name}[{len(octets)}] = " + "{")

    while len(octets) > 0:
        print("  " + ", ".join(octets[:13]) + ",")
        octets = octets[13:]

    print("};")
    print()

    return block_name


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-spki",
        action="store_true",
        help="Create a list of subject public key info fields",
    )
    parser.add_argument(
        "-dn",
        action="store_true",
        help="Create a list of subject distinguished name fields",
    )
    parser.add_argument("-listname", help="Name of the final DataAndLength block")
    parser.add_argument(
        "certId", nargs="+", help="A list of PEM files on disk or crt.sh IDs"
    )
    args = parser.parse_args()

    if not args.dn and not args.spki:
        parser.print_help()
        raise Exception("You must select either DN or SPKI matching")

    blocks = []

    print(
        "// Script from security/manager/tools/crtshToIdentifyingStruct/"
        + "crtshToIdentifyingStruct.py"
    )
    print("// Invocation: {}".format(" ".join(sys.argv)))
    print()

    identifierType = None
    if args.dn:
        identifierType = "DN"
    else:
        identifierType = "SPKI"

    for certId in args.certId:
        # Try a local file first, then crt.sh
        try:
            with open(certId, "rb") as pemFile:
                blocks.append(
                    print_block(pemFile.read(), identifierType=identifierType)
                )
        except OSError:
            r = requests.get(f"https://crt.sh/?d={certId}")
            r.raise_for_status()
            blocks.append(
                print_block(r.content, crtshId=certId, identifierType=identifierType)
            )

    print("static const DataAndLength " + args.listname + "[]= {")
    for structName in blocks:
        if len(structName) < 33:
            print("  { " + f"{structName}, sizeof({structName}) " + "},")
        else:
            print("  { " + f"{structName},")
            print(f"    sizeof({structName})" + " },")
    print("};")
