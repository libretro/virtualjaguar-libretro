import CryptoKit
import Foundation

// Verify with the same public key embedded in the shipping bundle.
let args = CommandLine.arguments
guard args.count == 4,
      let publicKey = Data(base64Encoded: args[2]),
      let signature = Data(base64Encoded: args[3]) else {
    fatalError("Expected archive path, base64 public key and base64 signature")
}
let key = try Curve25519.Signing.PublicKey(rawRepresentation: publicKey)
let archive = try Data(contentsOf: URL(fileURLWithPath: args[1]), options: .mappedIfSafe)
guard key.isValidSignature(signature, for: archive) else {
    FileHandle.standardError.write(Data("Invalid Sparkle archive signature\n".utf8))
    exit(1)
}
