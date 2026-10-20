package main

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"sort"
	"strings"
	"time"
)

const (
	maxPublicKeyBytes  = 64 << 10
	maxPrivateKeyBytes = 128 << 10
	maxPlaintextBytes  = 64 << 20
	maxCiphertextBytes = 96 << 20
)

var buildVersion = "0.1.0"

var errHelp = errors.New("help requested")

type stringList []string

func (s *stringList) String() string { return strings.Join(*s, ",") }
func (s *stringList) Set(value string) error {
	*s = append(*s, value)
	return nil
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}

func run(args []string, stdout, stderr io.Writer) int {
	if len(args) == 0 {
		usage(stderr)
		return 2
	}
	var err error
	switch args[0] {
	case "keygen":
		err = cmdKeygen(args[1:], stdout, stderr)
	case "register":
		err = cmdRegister(args[1:], stdout, stderr)
	case "status":
		err = cmdStatus(args[1:], stdout, stderr)
	case "encrypt":
		err = cmdEncrypt(args[1:], stdout, stderr)
	case "decrypt":
		err = cmdDecrypt(args[1:], stdout, stderr)
	case "keyring":
		err = cmdKeyring(args[1:], stdout, stderr)
	case "version":
		err = cmdVersion(stdout)
	case "help", "-h", "--help":
		usage(stdout)
		return 0
	default:
		usage(stderr)
		return 2
	}
	if err == nil {
		return 0
	}
	if errors.Is(err, errHelp) {
		return 0
	}
	fmt.Fprintln(stderr, "error:", err)
	return 1
}

func usage(w io.Writer) {
	fmt.Fprintln(w, `usage:
  hrt-f2f keygen   --name NAME --email EMAIL --passphrase-file FILE
                   --private-out FILE --public-out FILE [--force]
  hrt-f2f register --server URL --token-file FILE --public-key FILE
                   [--fingerprint FPR] [--proxy SOCKS5H_URL] [--json]
  hrt-f2f status   --server URL --token-file FILE [--proxy SOCKS5H_URL] [--json]
  hrt-f2f keyring list FILE
  hrt-f2f keyring merge --out FILE FILE...
  hrt-f2f keyring pull  --server URL --token-file FILE --out FILE
                        [--proxy SOCKS5H_URL] [--force]
  hrt-f2f encrypt  --recipient FILE [--recipient FILE ...] [--escrow FILE]
                   [--signer-private FILE --signer-passphrase-file FILE]
                   --in FILE --out FILE [--binary] [--force]
  hrt-f2f decrypt  --private FILE --passphrase-file FILE
                   [--verify-key FILE] --in FILE --out FILE [--force]
  hrt-f2f version

Private keys stay on this machine and are never uploaded. The server only
stores public keys. Verify member fingerprints face to face before trusting
a keyring. A passphrase file must be mode 0600 and contain at least 12 bytes.`)
}

func newFlagSet(name string, stderr io.Writer) *flag.FlagSet {
	fs := flag.NewFlagSet(name, flag.ContinueOnError)
	fs.SetOutput(stderr)
	return fs
}

func parseFlags(fs *flag.FlagSet, args []string) error {
	if err := fs.Parse(args); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return errHelp
		}
		return err
	}
	if fs.NArg() != 0 {
		return fmt.Errorf("unexpected positional arguments: %s", strings.Join(fs.Args(), " "))
	}
	return nil
}

func cmdVersion(stdout io.Writer) error {
	fmt.Fprintf(stdout, "hrt-f2f %s\n", buildVersion)
	return nil
}

func networkClient(server, tokenFile, proxyURL string, timeoutSeconds int) (*apiClient, error) {
	token, err := readTokenFile(tokenFile)
	if err != nil {
		return nil, err
	}
	if timeoutSeconds < 1 || timeoutSeconds > 300 {
		return nil, errors.New("--timeout must be between 1 and 300 seconds")
	}
	return newAPIClient(server, token, proxyURL, time.Duration(timeoutSeconds)*time.Second)
}

func cmdKeygen(args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("keygen", stderr)
	name := fs.String("name", "", "key owner name")
	email := fs.String("email", "", "key owner email")
	passFile := fs.String("passphrase-file", "", "passphrase file (0600)")
	privateOut := fs.String("private-out", "", "private key output")
	publicOut := fs.String("public-out", "", "public key output")
	force := fs.Bool("force", false, "replace existing output files")
	if err := parseFlags(fs, args); err != nil {
		return err
	}
	if *privateOut == "" || *publicOut == "" {
		return errors.New("--private-out and --public-out are required")
	}
	passphrase, err := readPassphraseFile(*passFile, true)
	if err != nil {
		return err
	}
	key, err := generateF2FKey(*name, *email, passphrase)
	if err != nil {
		return err
	}
	if err := writeOutput(*privateOut, []byte(key.PrivateArmor+"\n"), *force); err != nil {
		return err
	}
	if err := writeOutput(*publicOut, []byte(key.PublicArmor+"\n"), *force); err != nil {
		return err
	}
	fmt.Fprintf(stdout, "fingerprint: %s\n", key.Fingerprint)
	fmt.Fprintf(stdout, "private key: %s (mode 0600)\n", *privateOut)
	fmt.Fprintf(stdout, "public key: %s\n", *publicOut)
	return nil
}

func cmdRegister(args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("register", stderr)
	server := fs.String("server", "", "F2F server URL")
	tokenFile := fs.String("token-file", "", "web session token file (0600)")
	publicKeyFile := fs.String("public-key", "", "armored public key file")
	fingerprintFlag := fs.String("fingerprint", "", "expected fingerprint")
	proxyURL := fs.String("proxy", "", "socks5h:// proxy")
	timeout := fs.Int("timeout", 30, "request timeout in seconds")
	jsonOut := fs.Bool("json", false, "print the raw JSON response")
	if err := parseFlags(fs, args); err != nil {
		return err
	}
	if *publicKeyFile == "" {
		return errors.New("--public-key is required")
	}
	publicKeyBytes, err := readLimited(*publicKeyFile, maxPublicKeyBytes)
	if err != nil {
		return err
	}
	publicKey := strings.TrimSpace(string(publicKeyBytes))
	fingerprint := canonicalFingerprint(*fingerprintFlag)
	if fingerprint == "" {
		fingerprint, err = publicKeyFingerprint(publicKey)
		if err != nil {
			return err
		}
	}
	client, err := networkClient(*server, *tokenFile, *proxyURL, *timeout)
	if err != nil {
		return err
	}
	result, err := client.Register(publicKey, fingerprint)
	if err != nil {
		return err
	}
	if *jsonOut {
		encoded, _ := json.MarshalIndent(result, "", "  ")
		fmt.Fprintln(stdout, string(encoded))
		return nil
	}
	fmt.Fprintf(stdout, "registered %s in cohort %s at seat %d\n",
		result.Fingerprint, result.CohortID, result.Seat)
	return nil
}

func cmdStatus(args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("status", stderr)
	server := fs.String("server", "", "F2F server URL")
	tokenFile := fs.String("token-file", "", "web session token file (0600)")
	proxyURL := fs.String("proxy", "", "socks5h:// proxy")
	timeout := fs.Int("timeout", 30, "request timeout in seconds")
	jsonOut := fs.Bool("json", false, "print the raw JSON response")
	if err := parseFlags(fs, args); err != nil {
		return err
	}
	client, err := networkClient(*server, *tokenFile, *proxyURL, *timeout)
	if err != nil {
		return err
	}
	status, err := client.Status()
	if err != nil {
		return err
	}
	if *jsonOut {
		encoded, _ := json.MarshalIndent(status, "", "  ")
		fmt.Fprintln(stdout, string(encoded))
		return nil
	}
	if !status.Registered || status.Cohort == nil {
		fmt.Fprintln(stdout, "not registered in an F2F cohort")
		return nil
	}
	fmt.Fprintf(stdout, "cohort: %s (%s)\n", status.Cohort.ID, status.Cohort.State)
	fmt.Fprintf(stdout, "seat: %d\n", status.Seat)
	fmt.Fprintf(stdout, "members: %d/%d\n", status.Cohort.MemberCount, status.Cohort.TotalSeats)
	if status.Key != nil {
		fmt.Fprintf(stdout, "active key: %s\n", status.Key.Fingerprint)
	}
	if status.Escrow != nil {
		fmt.Fprintf(stdout, "escrow key: %s\n", status.Escrow.Fingerprint)
	}
	return nil
}

func cmdEncrypt(args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("encrypt", stderr)
	var recipients stringList
	fs.Var(&recipients, "recipient", "public key or keyring file (repeatable)")
	escrowFile := fs.String("escrow", "", "cohort escrow public key file")
	signerPrivate := fs.String("signer-private", "", "private key used to sign")
	signerPassFile := fs.String("signer-passphrase-file", "", "signer passphrase file")
	input := fs.String("in", "", "plaintext input")
	output := fs.String("out", "", "ciphertext output")
	binary := fs.Bool("binary", false, "write binary instead of armored output")
	force := fs.Bool("force", false, "replace an existing output file")
	if err := parseFlags(fs, args); err != nil {
		return err
	}
	if *input == "" || *output == "" {
		return errors.New("--in and --out are required")
	}
	if len(recipients) == 0 && *escrowFile == "" {
		return errors.New("at least one --recipient or --escrow is required")
	}
	if (*signerPrivate == "") != (*signerPassFile == "") {
		return errors.New("--signer-private and --signer-passphrase-file must be used together")
	}

	var recipientArmor []string
	for _, path := range recipients {
		data, err := readLimited(path, maxKeyringBytes)
		if err != nil {
			return fmt.Errorf("recipient %s: %w", path, err)
		}
		recipientArmor = append(recipientArmor, string(data))
	}
	if *escrowFile != "" {
		data, err := readLimited(*escrowFile, maxKeyringBytes)
		if err != nil {
			return fmt.Errorf("escrow key: %w", err)
		}
		recipientArmor = append(recipientArmor, string(data))
	}
	plaintext, err := readLimited(*input, maxPlaintextBytes)
	if err != nil {
		return err
	}
	signerArmor := ""
	var signerPassphrase []byte
	if *signerPrivate != "" {
		data, err := readLimited(*signerPrivate, maxPrivateKeyBytes)
		if err != nil {
			return err
		}
		signerArmor = string(data)
		signerPassphrase, err = readPassphraseFile(*signerPassFile, true)
		if err != nil {
			return err
		}
	}

	ciphertext, fingerprints, signer, err := encryptF2F(
		recipientArmor, signerArmor, signerPassphrase, plaintext, !*binary,
	)
	if err != nil {
		return err
	}
	if err := writeOutput(*output, ciphertext, *force); err != nil {
		return err
	}
	fmt.Fprintf(stdout, "encrypted for: %s\n", strings.Join(fingerprints, ", "))
	if signer != "" {
		fmt.Fprintf(stdout, "signed by: %s\n", signer)
	}
	fmt.Fprintf(stdout, "output: %s\n", *output)
	return nil
}

func cmdDecrypt(args []string, stdout, stderr io.Writer) error {
	fs := newFlagSet("decrypt", stderr)
	privateFile := fs.String("private", "", "armored private key")
	passFile := fs.String("passphrase-file", "", "private key passphrase file")
	verifyFile := fs.String("verify-key", "", "require a signature from this public key")
	input := fs.String("in", "", "ciphertext input")
	output := fs.String("out", "", "plaintext output")
	force := fs.Bool("force", false, "replace an existing output file")
	if err := parseFlags(fs, args); err != nil {
		return err
	}
	if *privateFile == "" || *passFile == "" || *input == "" || *output == "" {
		return errors.New("--private, --passphrase-file, --in, and --out are required")
	}
	privateBytes, err := readLimited(*privateFile, maxPrivateKeyBytes)
	if err != nil {
		return err
	}
	passphrase, err := readPassphraseFile(*passFile, true)
	if err != nil {
		return err
	}
	ciphertext, err := readLimited(*input, maxCiphertextBytes)
	if err != nil {
		return err
	}
	verifyArmor := ""
	if *verifyFile != "" {
		data, err := readLimited(*verifyFile, maxPublicKeyBytes)
		if err != nil {
			return err
		}
		verifyArmor = string(data)
	}
	plaintext, signer, err := decryptF2F(
		string(privateBytes), passphrase, ciphertext, verifyArmor, maxPlaintextBytes,
	)
	if err != nil {
		return err
	}
	if err := writeOutput(*output, plaintext, *force); err != nil {
		return err
	}
	if signer != "" {
		fmt.Fprintf(stdout, "verified signer: %s\n", signer)
	}
	fmt.Fprintf(stdout, "output: %s\n", *output)
	return nil
}

func cmdKeyring(args []string, stdout, stderr io.Writer) error {
	if len(args) == 0 {
		return errors.New("keyring needs list, merge, or pull")
	}
	switch args[0] {
	case "list":
		if len(args) != 2 {
			return errors.New("usage: hrt-f2f keyring list FILE")
		}
		data, err := readLimited(args[1], maxKeyringBytes)
		if err != nil {
			return err
		}
		infos, err := listKeyring(string(data))
		if err != nil {
			return err
		}
		sort.Slice(infos, func(i, j int) bool { return infos[i].Fingerprint < infos[j].Fingerprint })
		for _, info := range infos {
			fmt.Fprintf(stdout, "%s", info.Fingerprint)
			if len(info.UserIDs) > 0 {
				fmt.Fprintf(stdout, "  %s", strings.Join(info.UserIDs, ", "))
			}
			fmt.Fprintln(stdout)
		}
		return nil
	case "merge":
		fs := newFlagSet("keyring merge", stderr)
		output := fs.String("out", "", "merged keyring output")
		force := fs.Bool("force", false, "replace an existing output file")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if *output == "" || fs.NArg() == 0 {
			return errors.New("usage: hrt-f2f keyring merge --out FILE FILE...")
		}
		var armors []string
		for _, path := range fs.Args() {
			data, err := readLimited(path, maxKeyringBytes)
			if err != nil {
				return err
			}
			armors = append(armors, string(data))
		}
		merged, fingerprints, err := mergeKeyrings(armors)
		if err != nil {
			return err
		}
		if err := writeOutput(*output, []byte(merged), *force); err != nil {
			return err
		}
		fmt.Fprintf(stdout, "merged %d keys into %s\n", len(fingerprints), *output)
		return nil
	case "pull":
		fs := newFlagSet("keyring pull", stderr)
		server := fs.String("server", "", "F2F server URL")
		tokenFile := fs.String("token-file", "", "web session token file (0600)")
		proxyURL := fs.String("proxy", "", "socks5h:// proxy")
		timeout := fs.Int("timeout", 30, "request timeout in seconds")
		output := fs.String("out", "", "keyring output")
		force := fs.Bool("force", false, "replace an existing output file")
		if err := parseFlags(fs, args[1:]); err != nil {
			return err
		}
		if *output == "" {
			return errors.New("--out is required")
		}
		client, err := networkClient(*server, *tokenFile, *proxyURL, *timeout)
		if err != nil {
			return err
		}
		status, err := client.Status()
		if err != nil {
			return err
		}
		if !status.Registered || status.Cohort == nil {
			return errors.New("this account is not in an F2F cohort")
		}
		keyring, err := client.Keyring(status.Cohort.ID)
		if err != nil {
			return err
		}
		if err := writeOutput(*output, []byte(keyring), *force); err != nil {
			return err
		}
		infos, _ := listKeyring(keyring)
		fmt.Fprintf(stdout, "cohort %s, seat %d, %d public keys\n",
			status.Cohort.ID, status.Seat, len(infos))
		fmt.Fprintf(stdout, "output: %s\n", *output)
		return nil
	default:
		return errors.New("keyring needs list, merge, or pull")
	}
}
