/*
 * test_mul_depth_8192_4_ABCD.cpp
 *
 *  Created on: 22 Dec 2023
 *      Author: massimiliano
 */

#include <iostream>
#include "seal/seal.h"
#include "utils.h"

using namespace std;
using namespace seal;

/*
 * calculate A * B * C * D to test multiply depth
 */

void test_mul_depth_8192_4_ABCD(double range_limit = 100.0){

	cout << "test_mul_depth_8192_4_ABCD()" << endl;
	cout << "input data range [" << -range_limit << ", " << range_limit << "]" << endl;

	EncryptionParameters parms(scheme_type::ckks);

	/*
	 * poly_modulus degree 8192
	 * primes coeff_modulus {60,40,40,60} max_coeff_modulus 218
	 * scale 2^40 precision before point 60-40 = 20 bit, precision after point 40-20 = 20 Bit
	 */
	size_t poly_modulus_degree = 8192;
	size_t scale_exp = 40;
	parms.set_poly_modulus_degree(poly_modulus_degree);
    parms.set_coeff_modulus(CoeffModulus::Create(poly_modulus_degree, {60,40,40,60}));
    double scale = pow(2.0, scale_exp);

    /*
     * SEAL context
     */
    SEALContext context(parms);
    print_parameters(context);
    cout << "context.using_keyswitching()? " << context.using_keyswitching() << endl;
    cout << endl;

    print_modulus_switching_chain(context);

    /*
     * key generation
     */
    KeyGenerator keygen(context);
    SecretKey secret_key = keygen.secret_key();
    RelinKeys relin_keys;
    keygen.create_relin_keys(relin_keys);
    GaloisKeys gal_keys;
    keygen.create_galois_keys(gal_keys);

    cout << "Print the parameter IDs of generated keys." << endl;
    cout << "    + secret_key:  " << secret_key.parms_id() << endl;
    cout << "    + relin_keys:  " << relin_keys.parms_id() << endl << endl;

    /*
     * encryptor, decryptor, evaluator and encoder
     */
    Encryptor encryptor(context, secret_key);
    Evaluator evaluator(context);
    Decryptor decryptor(context, secret_key);

    CKKSEncoder encoder(context);
    size_t slot_count = encoder.slot_count();
    cout << "Encoder number of slots: " << slot_count << endl;
    cout << "Scale 2^" << scale_exp << endl << endl;

    /*
     * random input data and expected result
     */
    const vector<double> input_A = generate_random_data(10, -range_limit, range_limit);
    const vector<double> input_B = generate_random_data(10, -range_limit, range_limit);
    const vector<double> input_C = generate_random_data(10, -range_limit, range_limit);
    const vector<double> input_D = generate_random_data(10, -range_limit, range_limit);

    cout << "Input A vector size " << input_A.size() << endl;
    print_vector(input_A);
    cout << "Input B vector size " << input_B.size() << endl;
    print_vector(input_B);
    cout << "Input C vector size " << input_C.size() << endl;
    print_vector(input_C);
    cout << "Input D vector size " << input_D.size() << endl;
    print_vector(input_D);

    vector<double> expected_AB;
    vector<double> expected_ABC;
    vector<double> expected_ABCD;
    for(int i=0;i<input_A.size();i++){
    	expected_AB.push_back(input_A[i]*input_B[i]);
    	expected_ABC.push_back(expected_AB[i]*input_C[i]);
    	expected_ABCD.push_back(expected_ABC[i]*input_D[i]);
    }

    cout << "--------------------------" << endl << endl;

    /*
     * encode input vector
     */
    Plaintext plain_A, plain_B, plain_C, plain_D;
    encoder.encode(input_A, scale, plain_A);
    encoder.encode(input_B, scale, plain_B);
    encoder.encode(input_C, scale, plain_C);
    encoder.encode(input_D, scale, plain_D);

    cout << "Input A plaintext" << endl;
    print_plaintext_info(plain_A,context);
    cout << "Input B plaintext" << endl;
    print_plaintext_info(plain_B,context);
    cout << "Input C plaintext" << endl;
    print_plaintext_info(plain_C,context);
    cout << "Input D plaintext" << endl;
    print_plaintext_info(plain_D,context);

    cout << "--------------------------" << endl << endl;

    /*
     * encrypt plaintext
     */
    Ciphertext encrypted_A, encrypted_B, encrypted_C, encrypted_D;
    encryptor.encrypt_symmetric(plain_A, encrypted_A);
    encryptor.encrypt_symmetric(plain_B, encrypted_B);
    encryptor.encrypt_symmetric(plain_C, encrypted_C);
    encryptor.encrypt_symmetric(plain_D, encrypted_D);

    cout << "Input A ciphertext" << endl;
    print_ciphertext_info(encrypted_A,context);
    cout << "Input B ciphertext" << endl;
    print_ciphertext_info(encrypted_B,context);
    cout << "Input C ciphertext" << endl;
    print_ciphertext_info(encrypted_C,context);
    cout << "Input D ciphertext" << endl;
    print_ciphertext_info(encrypted_D,context);

    cout << "--------------------------" << endl << endl;

    /*
     * perform multiplication A*B
     */
    Ciphertext encrypted_AB;
    evaluator.multiply(encrypted_A, encrypted_B, encrypted_AB);

    cout << "Result AB" << endl;
    print_ciphertext_info(encrypted_AB,context);

    check_chiphertext(&decryptor, &encoder, encrypted_AB, expected_AB);

    cout << "--------------------------" << endl << endl;

    /*
     * relinearize A*B to return to size = 2
     */
    evaluator.relinearize_inplace(encrypted_AB,relin_keys);

    cout << "Result AB relin" << endl;
    print_ciphertext_info(encrypted_AB,context);

    check_chiphertext(&decryptor, &encoder, encrypted_AB, expected_AB);

    /*
     * rescale A*B to next prime in the chain
     */
    evaluator.rescale_to_next_inplace(encrypted_AB);

    cout << "Result AB rescale" << endl;
    print_ciphertext_info(encrypted_AB,context);

    check_chiphertext(&decryptor, &encoder, encrypted_AB, expected_AB);

    /*
     * switch C to the same prime as A*B
     */
    evaluator.mod_switch_to_inplace(encrypted_C, encrypted_AB.parms_id());

    cout << "Input C mod switch" << endl;
    print_ciphertext_info(encrypted_C,context);

    cout << "--------------------------" << endl << endl;

    /*
     * check scale
     */
    if(!check_operand_scale(encrypted_AB, encrypted_C)){

		/*
		 * fix the scale of A*B by forcing the expected value
		 */
		encrypted_AB.scale() = pow(2.0,scale_exp);

		cout << "Result AB fix scale" << endl;
		print_ciphertext_info(encrypted_AB,context);

		check_chiphertext(&decryptor, &encoder, encrypted_AB, expected_AB);
    }

    /*
     * perform multiplication (A*B)*C
     */
    Ciphertext encrypted_ABC;
    evaluator.multiply(encrypted_AB, encrypted_C, encrypted_ABC);

    cout << "Result ABC" << endl;
    print_ciphertext_info(encrypted_ABC,context);

    check_chiphertext(&decryptor, &encoder, encrypted_ABC, expected_ABC);

    cout << "--------------------------" << endl << endl;


    /*
     * relinearize A*B*C to return to size = 2
     */
    evaluator.relinearize_inplace(encrypted_ABC,relin_keys);

    cout << "Result ABC relin" << endl;
    print_ciphertext_info(encrypted_ABC,context);

    check_chiphertext(&decryptor, &encoder, encrypted_ABC, expected_ABC);

    /*
     * rescale A*B*C to next prime in the chain
     */
    evaluator.rescale_to_next_inplace(encrypted_ABC);

    cout << "Result ABC rescale" << endl;
    print_ciphertext_info(encrypted_ABC,context);

    check_chiphertext(&decryptor, &encoder, encrypted_ABC, expected_ABC);

    /*
     * switch D to the same prime as A*B*C
     */
    evaluator.mod_switch_to_inplace(encrypted_D, encrypted_ABC.parms_id());

    cout << "Input D mod switch" << endl;
    print_ciphertext_info(encrypted_D,context);

    cout << "--------------------------" << endl << endl;

    /*
     * check scale
     */
    if(!check_operand_scale(encrypted_ABC, encrypted_D)){

		/*
		 * fix the scale of A*B*C by forcing the expected value
		 */
		encrypted_ABC.scale() = pow(2.0,scale_exp);

		cout << "Result ABC fix scale" << endl;
		print_ciphertext_info(encrypted_ABC,context);

		check_chiphertext(&decryptor, &encoder, encrypted_ABC, expected_ABC);
    }

    /*
     * perform multiplication ((A*B)*C)*D
     */
    Ciphertext encrypted_ABCD;
    evaluator.multiply(encrypted_ABC, encrypted_D, encrypted_ABCD);

    cout << "Result ABCD" << endl;
    print_ciphertext_info(encrypted_ABCD,context);

    check_chiphertext(&decryptor, &encoder, encrypted_ABCD, expected_ABCD);

}

