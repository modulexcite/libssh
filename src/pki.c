/*
 * known_hosts.c
 * This file is part of the SSH Library
 *
 * Copyright (c) 2010 by Aris Adamantiadis
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

/**
 * @defgroup libssh_pki The SSH Public Key Infrastructure
 * @ingroup libssh
 *
 * Functions for the creation, importation and manipulation of public and
 * private keys in the context of the SSH protocol
 *
 * @{
 */

#include "libssh/libssh.h"
#include "libssh/session.h"
#include "libssh/priv.h"
#include "libssh/pki.h"
#include "libssh/keys.h"
#include "libssh/buffer.h"

/**
 * @brief creates a new empty SSH key
 * @returns an empty ssh_key handle, or NULL on error.
 */
ssh_key ssh_key_new (void) {
  ssh_key ptr = malloc (sizeof (struct ssh_key_struct));
  if (ptr == NULL) {
      return NULL;
  }
  ZERO_STRUCTP(ptr);
  return ptr;
}

/**
 * @brief clean up the key and deallocate all existing keys
 * @param[in] key ssh_key to clean
 */
void ssh_key_clean (ssh_key key){
    if(key == NULL)
        return;
#ifdef HAVE_LIBGCRYPT
    if(key->dsa) gcry_sexp_release(key->dsa);
    if(key->rsa) gcry_sexp_release(key->rsa);
#elif defined HAVE_LIBCRYPTO
    if(key->dsa) DSA_free(key->dsa);
    if(key->rsa) RSA_free(key->rsa);
#endif
    key->flags=SSH_KEY_FLAG_EMPTY;
    key->type=SSH_KEYTYPE_UNKNOWN;
    key->type_c=NULL;
    key->dsa = NULL;
    key->rsa = NULL;
}

/**
 * @brief deallocate a SSH key
 * @param[in] key ssh_key handle to free
 */
void ssh_key_free (ssh_key key){
    if(key){
        ssh_key_clean(key);
        SAFE_FREE(key);
    }
}

/**
 * @brief returns the type of a ssh key
 * @param[in] key the ssh_key handle
 * @returns one of SSH_KEYTYPE_RSA,SSH_KEYTYPE_DSS,SSH_KEYTYPE_RSA1
 * @returns SSH_KEYTYPE_UNKNOWN if the type is unknown
 */
enum ssh_keytypes_e ssh_key_type(ssh_key key){
    if (key == NULL) {
        return SSH_KEYTYPE_UNKNOWN;
    }
    return key->type;
}

/**
 * @brief import a key from a file
 * @param[out]  key      the ssh_key to update
 * @param[in]  session  The SSH Session to use. If a key decryption callback is set, it will
 *                      be used to ask for the passphrase.
 * @param[in]  filename The filename of the the private key.
 * @param[in]  passphrase The passphrase to decrypt the private key. Set to null
 *                        if none is needed or it is unknown.
 * @returns SSH_OK on success, SSH_ERROR otherwise.
 **/
int ssh_key_import_private(ssh_key key, ssh_session session, const char *filename, const char *passphrase){
  ssh_private_key priv=privatekey_from_file(session,filename,0,passphrase);
  if(priv==NULL)
    return SSH_ERROR;
  ssh_key_clean(key);
  key->dsa=priv->dsa_priv;
  key->rsa=priv->rsa_priv;
  key->type=priv->type;
  key->flags=SSH_KEY_FLAG_PRIVATE | SSH_KEY_FLAG_PUBLIC;
  key->type_c=ssh_type_to_char(key->type);
  SAFE_FREE(priv);
  return SSH_OK;
}

/* temporary function to migrate seemlessly to ssh_key */
ssh_public_key ssh_pki_convert_key_to_publickey(ssh_key key) {
    ssh_public_key pub;

    if(key == NULL) {
        return NULL;
    }

    pub = malloc(sizeof(struct ssh_public_key_struct));
    if (pub == NULL) {
        return NULL;
    }
    ZERO_STRUCTP(pub);

    pub->dsa_pub = key->dsa;
    pub->rsa_pub = key->rsa;
    pub->type     = key->type;
    pub->type_c   = key->type_c;

    return pub;
}

/** @brief import a base64 formated key from a memory c-string
 *
 * @param   key     The key to fill, created with ssh_key_new()
 * @param   session The ssh session
 * @param   b64_key The c-string holding the base64 encoded key
 * @param   passphrase  The passphrase to decrypt the key, or NULL
 *
 * @return  SSH_ERROR in case of error, SSH_OK otherwise
 */
int ssh_pki_import_privkey_base64(ssh_key key, ssh_session session,
                    const char *b64_key, const char *passphrase) {
    ssh_private_key priv;

    if(key == NULL || session == NULL) {
        return SSH_ERROR;
    }

    if(b64_key == NULL || !*b64_key) {
        return SSH_ERROR;
    }

    priv = privatekey_from_base64(session, b64_key, 0, passphrase);
    if(priv == NULL) {
        return SSH_ERROR;
    }

    ssh_key_clean(key);

    key->dsa = priv->dsa_priv;
    key->rsa = priv->rsa_priv;
    key->type = priv->type;
    key->flags = SSH_KEY_FLAG_PRIVATE | SSH_KEY_FLAG_PUBLIC;
    key->type_c = ssh_type_to_char(key->type);

    SAFE_FREE(priv);
    return SSH_OK;
}

ssh_key ssh_pki_publickey_from_privatekey(ssh_key privkey) {
    ssh_key pubkey = NULL;
#ifdef HAVE_LIBGCRYPT
    gcry_sexp_t sexp;
    const char *tmp = NULL;
    size_t size;
    ssh_string p = NULL;
    ssh_string q = NULL;
    ssh_string g = NULL;
    ssh_string y = NULL;
    ssh_string e = NULL;
    ssh_string n = NULL;
#endif /* HAVE_LIBGCRYPT */

    if(privkey == NULL || !ssh_key_is_private(privkey)) {
        return NULL;
    }

    pubkey = malloc(sizeof(struct ssh_key_struct));
    if (pubkey == NULL) {
        return NULL;
    }
    ZERO_STRUCTP(pubkey);
    pubkey->type = privkey->type;
    switch(pubkey->type) {
        case SSH_KEYTYPE_DSS:
#ifdef HAVE_LIBGCRYPT
            sexp = gcry_sexp_find_token(privkey->dsa, "p", 0);
            if (sexp == NULL) {
                goto error;
            }
            tmp = gcry_sexp_nth_data(sexp, 1, &size);
            p = ssh_string_new(size);
            if (p == NULL) {
                goto error;
            }
            ssh_string_fill(p,(char *) tmp, size);
            gcry_sexp_release(sexp);

            sexp = gcry_sexp_find_token(privkey->dsa,"q",0);
            if (sexp == NULL) {
                goto error;
            }
            tmp = gcry_sexp_nth_data(sexp,1,&size);
            q = ssh_string_new(size);
            if (q == NULL) {
                goto error;
            }
            ssh_string_fill(q,(char *) tmp,size);
            gcry_sexp_release(sexp);

            sexp = gcry_sexp_find_token(privkey->dsa, "g", 0);
            if (sexp == NULL) {
                goto error;
            }
            tmp = gcry_sexp_nth_data(sexp,1,&size);
            g = ssh_string_new(size);
            if (g == NULL) {
                goto error;
            }
            ssh_string_fill(g,(char *) tmp,size);
            gcry_sexp_release(sexp);

            sexp = gcry_sexp_find_token(privkey->dsa,"y",0);
            if (sexp == NULL) {
                goto error;
            }
            tmp = gcry_sexp_nth_data(sexp,1,&size);
            y = ssh_string_new(size);
            if (y == NULL) {
                goto error;
            }
            ssh_string_fill(y,(char *) tmp,size);
            gcry_sexp_release(sexp);

            gcry_sexp_build(&pubkey->dsa, NULL,
                    "(public-key(dsa(p %b)(q %b)(g %b)(y %b)))",
                    ssh_string_len(p), ssh_string_data(p),
                    ssh_string_len(q), ssh_string_data(q),
                    ssh_string_len(g), ssh_string_data(g),
                    ssh_string_len(y), ssh_string_data(y));

            ssh_string_burn(p);
            ssh_string_free(p);
            ssh_string_burn(q);
            ssh_string_free(q);
            ssh_string_burn(g);
            ssh_string_free(g);
            ssh_string_burn(y);
            ssh_string_free(y);
#elif defined HAVE_LIBCRYPTO
            pubkey->dsa = DSA_new();
            if (pubkey->dsa == NULL) {
                goto error;
            }
            pubkey->dsa->p = BN_dup(privkey->dsa->p);
            pubkey->dsa->q = BN_dup(privkey->dsa->q);
            pubkey->dsa->g = BN_dup(privkey->dsa->g);
            pubkey->dsa->pub_key = BN_dup(privkey->dsa->pub_key);
            if (pubkey->dsa->p == NULL ||
                    pubkey->dsa->q == NULL ||
                    pubkey->dsa->g == NULL ||
                    pubkey->dsa->pub_key == NULL) {
                goto error;
            }
#endif /* HAVE_LIBCRYPTO */
            break;
        case SSH_KEYTYPE_RSA:
        case SSH_KEYTYPE_RSA1:
#ifdef HAVE_LIBGCRYPT
            sexp = gcry_sexp_find_token(privkey->rsa, "n", 0);
            if (sexp == NULL) {
                goto error;
            }
            tmp = gcry_sexp_nth_data(sexp, 1, &size);
            n = ssh_string_new(size);
            if (n == NULL) {
                goto error;
            }
            ssh_string_fill(n, (char *) tmp, size);
            gcry_sexp_release(sexp);

            sexp = gcry_sexp_find_token(privkey->rsa, "e", 0);
            if (sexp == NULL) {
                goto error;
            }
            tmp = gcry_sexp_nth_data(sexp, 1, &size);
            e = ssh_string_new(size);
            if (e == NULL) {
                goto error;
            }
            ssh_string_fill(e, (char *) tmp, size);
            gcry_sexp_release(sexp);

            gcry_sexp_build(&pubkey->rsa, NULL,
                    "(public-key(rsa(n %b)(e %b)))",
                    ssh_string_len(n), ssh_string_data(n),
                    ssh_string_len(e), ssh_string_data(e));
            if (pubkey->rsa == NULL) {
                goto error;
            }

            ssh_string_burn(e);
            ssh_string_free(e);
            ssh_string_burn(n);
            ssh_string_free(n);
#elif defined HAVE_LIBCRYPTO
            pubkey->rsa = RSA_new();
            if (pubkey->rsa == NULL) {
                goto error;
            }
            pubkey->rsa->e = BN_dup(privkey->rsa->e);
            pubkey->rsa->n = BN_dup(privkey->rsa->n);
            if (pubkey->rsa->e == NULL ||
                    pubkey->rsa->n == NULL) {
                goto error;
            }
#endif
            break;
        default:
            ssh_key_free(pubkey);
            return NULL;
    }
    pubkey->type_c = ssh_type_to_char(privkey->type);

    return pubkey;
error:
#ifdef HAVE_LIBGCRYPT
    gcry_sexp_release(sexp);
    ssh_string_burn(p);
    ssh_string_free(p);
    ssh_string_burn(q);
    ssh_string_free(q);
    ssh_string_burn(g);
    ssh_string_free(g);
    ssh_string_burn(y);
    ssh_string_free(y);

    ssh_string_burn(e);
    ssh_string_free(e);
    ssh_string_burn(n);
    ssh_string_free(n);
#endif
    ssh_key_free(pubkey);

    return NULL;
}

/*
 * This function signs the session id (known as H) as a string then
 * the content of sigbuf */
ssh_string ssh_pki_do_sign(ssh_session session, ssh_buffer sigbuf,
    ssh_key privatekey) {
  struct ssh_crypto_struct *crypto = session->current_crypto ? session->current_crypto :
    session->next_crypto;
  unsigned char hash[SHA_DIGEST_LEN + 1] = {0};
  ssh_string session_str = NULL;
  ssh_string signature = NULL;
  SIGNATURE *sign = NULL;
  SHACTX ctx = NULL;
#ifdef HAVE_LIBGCRYPT
  gcry_sexp_t gcryhash;
#endif

  if(privatekey == NULL || !ssh_key_is_private(privatekey)) {
      return NULL;
  }

  session_str = ssh_string_new(SHA_DIGEST_LEN);
  if (session_str == NULL) {
    return NULL;
  }
  ssh_string_fill(session_str, crypto->session_id, SHA_DIGEST_LEN);

  ctx = sha1_init();
  if (ctx == NULL) {
    ssh_string_free(session_str);
    return NULL;
  }

  sha1_update(ctx, session_str, ssh_string_len(session_str) + 4);
  ssh_string_free(session_str);
  sha1_update(ctx, buffer_get_rest(sigbuf), buffer_get_rest_len(sigbuf));
  sha1_final(hash + 1,ctx);
  hash[0] = 0;

#ifdef DEBUG_CRYPTO
  ssh_print_hexa("Hash being signed with dsa", hash + 1, SHA_DIGEST_LEN);
#endif

  sign = malloc(sizeof(SIGNATURE));
  if (sign == NULL) {
    return NULL;
  }

  switch(privatekey->type) {
    case SSH_KEYTYPE_DSS:
#ifdef HAVE_LIBGCRYPT
      if (gcry_sexp_build(&gcryhash, NULL, "%b", SHA_DIGEST_LEN + 1, hash) ||
          gcry_pk_sign(&sign->dsa_sign, gcryhash, privatekey->dsa)) {
        ssh_set_error(session, SSH_FATAL, "Signing: libcrypt error");
        gcry_sexp_release(gcryhash);
        signature_free(sign);
        return NULL;
      }
#elif defined HAVE_LIBCRYPTO
      sign->dsa_sign = DSA_do_sign(hash + 1, SHA_DIGEST_LEN,
          privatekey->dsa);
      if (sign->dsa_sign == NULL) {
        ssh_set_error(session, SSH_FATAL, "Signing: openssl error");
        signature_free(sign);
        return NULL;
      }
#ifdef DEBUG_CRYPTO
      ssh_print_bignum("r", sign->dsa_sign->r);
      ssh_print_bignum("s", sign->dsa_sign->s);
#endif
#endif /* HAVE_LIBCRYPTO */
      sign->rsa_sign = NULL;
      break;
    case SSH_KEYTYPE_RSA:
#ifdef HAVE_LIBGCRYPT
      if (gcry_sexp_build(&gcryhash, NULL, "(data(flags pkcs1)(hash sha1 %b))",
            SHA_DIGEST_LEN, hash + 1) ||
          gcry_pk_sign(&sign->rsa_sign, gcryhash, privatekey->rsa)) {
        ssh_set_error(session, SSH_FATAL, "Signing: libcrypt error");
        gcry_sexp_release(gcryhash);
        signature_free(sign);
        return NULL;
      }
#elif defined HAVE_LIBCRYPTO
      sign->rsa_sign = RSA_do_sign(hash + 1, SHA_DIGEST_LEN,
          privatekey->rsa);
      if (sign->rsa_sign == NULL) {
        ssh_set_error(session, SSH_FATAL, "Signing: openssl error");
        signature_free(sign);
        return NULL;
      }
#endif
      sign->dsa_sign = NULL;
      break;
    default:
      signature_free(sign);
      return NULL;
  }
#ifdef HAVE_LIBGCRYPT
  gcry_sexp_release(gcryhash);
#endif

  sign->type = privatekey->type;

  signature = signature_to_string(sign);
  signature_free(sign);

  return signature;
}


/**
 * @}
 */
