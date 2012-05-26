#include "../src/hammer.h"
#include "dns_common.h"
#include "dns.h"

#define false 0
#define true 1

bool is_zero(HParseResult *p) {
  if (TT_UINT != p->ast->token_type)
    return false;
  return (0 == p->ast->uint);
}

/**
 * Every DNS message should have QDCOUNT entries in the question
 * section, and ANCOUNT+NSCOUNT+ARCOUNT resource records.
 */
bool validate_dns(HParseResult *p) {
  if (TT_SEQUENCE != p->ast->token_type)
    return false;
  // The header holds the counts as its last 4 elements.
  HParsedToken **elems = p->ast->seq->elements[0]->seq->elements;
  size_t qd = elems[8]->uint;
  size_t an = elems[9]->uint;
  size_t ns = elems[10]->uint;
  size_t ar = elems[11]->uint;
  HParsedToken *questions = p->ast->seq->elements[1];
  if (questions->seq->used != qd)
    return false;
  HParsedToken *rrs = p->ast->seq->elements[2];
  if (an+ns+ar != rrs->seq->used)
    return false;
  return true;
}

struct dns_qname get_qname(const HParsedToken *t) {
  // The qname parser parses at least 1 length-value pair, then a NULL.
  // So, t->seq->elements[0] is a sequence of at least 1 such pair,
  // and t->seq->elements[1] is the null.
  const HParsedToken *labels = t->seq->elements[0];
  struct dns_qname ret = {
    .qlen = labels->seq->used,
    .labels = h_arena_malloc(t->seq->arena, sizeof(ret.labels)*ret.qlen)
  };
  // i is which label we're on
  for (size_t i=0; i<labels->seq->used; ++i) {
    ret.labels[i].len = labels->seq->elements[i]->seq->used;
    ret.labels[i].label = h_arena_malloc(t->seq->arena, sizeof(uint8_t)*ret.labels[i].len);
    // j is which char of the label we're on
    for (size_t j=0; j<ret.labels[i].len; ++j)
      ret.labels[i].label[j] = labels->seq->elements[i]->seq->elements[j]->uint;
  }
  return ret;
}

char* get_domain(const HParsedToken *t) {
  switch(t->token_type) {
  case TT_UINT:
    return " ";
  case TT_SEQUENCE:
    {
      // Sequence of subdomains separated by "."
      return NULL;
    }
  default:
    return NULL;
  }

}

void set_rr(struct dns_rr rr, HCountedArray *rdata) {

}

const HParsedToken* pack_dns_struct(const HParseResult *p) {
  HParsedToken *ret = h_arena_malloc(p->arena, sizeof(HParsedToken*));
  ret->token_type = TT_USER;

  dns_message_t *msg = h_arena_malloc(p->arena, sizeof(dns_message_t*));

  HParsedToken *hdr = p->ast->seq->elements[0];
  struct dns_header header = {
    .id = hdr->seq->elements[0]->uint,
    .qr = hdr->seq->elements[1]->uint,
    .opcode = hdr->seq->elements[2]->uint,
    .aa = hdr->seq->elements[3]->uint,
    .tc = hdr->seq->elements[4]->uint,
    .rd = hdr->seq->elements[5]->uint,
    .ra = hdr->seq->elements[6]->uint,
    .rcode = hdr->seq->elements[7]->uint,
    .question_count = hdr->seq->elements[8]->uint,
    .answer_count = hdr->seq->elements[9]->uint,
    .authority_count = hdr->seq->elements[10]->uint,
    .additional_count = hdr->seq->elements[11]->uint
  };
  msg->header = header;

  HParsedToken *qs = p->ast->seq->elements[1];
  struct dns_question *questions = h_arena_malloc(p->arena,
						sizeof(struct dns_question)*(header.question_count));
  for (size_t i=0; i<header.question_count; ++i) {
    // QNAME is a sequence of labels. In the parser, it's defined as
    // sequence(many1(length_value(...)), ch('\x00'), NULL).
    questions[i].qname = get_qname(qs->seq->elements[i]->seq->elements[0]);
    questions[i].qtype = qs->seq->elements[i]->seq->elements[1]->uint;
    questions[i].qclass = qs->seq->elements[i]->seq->elements[2]->uint;
  }
  msg->questions = questions;

  HParsedToken *rrs = p->ast->seq->elements[2];
  struct dns_rr *answers = h_arena_malloc(p->arena,
					  sizeof(struct dns_rr)*(header.answer_count));
  for (size_t i=0; i<header.answer_count; ++i) {
    answers[i].name = get_domain(rrs[i].seq->elements[0]);
    answers[i].type = rrs[i].seq->elements[1]->uint;
    answers[i].type = rrs[i].seq->elements[2]->uint;
    answers[i].ttl = rrs[i].seq->elements[3]->uint;
    answers[i].rdlength = rrs[i].seq->elements[4]->seq->used;
    set_rr(answers[i], rrs[i].seq->elements[4]->seq);	   
  }
  msg->answers = answers;

  struct dns_rr *authority = h_arena_malloc(p->arena,
					  sizeof(struct dns_rr)*(header.authority_count));
  for (size_t i=0, j=header.answer_count; i<header.authority_count; ++i, ++j) {
    authority[i].name = get_domain(rrs[j].seq->elements[0]);
    authority[i].type = rrs[j].seq->elements[1]->uint;
    authority[i].type = rrs[j].seq->elements[2]->uint;
    authority[i].ttl = rrs[j].seq->elements[3]->uint;
    authority[i].rdlength = rrs[j].seq->elements[4]->seq->used;
    set_rr(authority[i], rrs[j].seq->elements[4]->seq);
  }
  msg->authority = authority;

  struct dns_rr *additional = h_arena_malloc(p->arena,
					     sizeof(struct dns_rr)*(header.additional_count));
  for (size_t i=0, j=header.answer_count+header.authority_count; i<header.additional_count; ++i, ++j) {
    additional[i].name = get_domain(rrs[j].seq->elements[0]);
    additional[i].type = rrs[j].seq->elements[1]->uint;
    additional[i].type = rrs[j].seq->elements[2]->uint;
    additional[i].ttl = rrs[j].seq->elements[3]->uint;
    additional[i].rdlength = rrs[j].seq->elements[4]->seq->used;
    set_rr(additional[i], rrs[j].seq->elements[4]->seq);
  }
  msg->additional = additional;

  ret->user = (void*)msg;
  return ret;
}

const HParser* init_parser() {
  static HParser *dns_message = NULL;
  if (dns_message)
    return dns_message;

  const HParser *domain = init_domain();

  const HParser *dns_header = h_sequence(h_bits(16, false), // ID
					 h_bits(1, false),  // QR
					 h_bits(4, false),  // opcode
					 h_bits(1, false),  // AA
					 h_bits(1, false),  // TC
					 h_bits(1, false),  // RD
					 h_bits(1, false),  // RA
					 h_ignore(h_attr_bool(h_bits(3, false), is_zero)), // Z
					 h_bits(4, false),  // RCODE
					 h_uint16(), // QDCOUNT
					 h_uint16(), // ANCOUNT
					 h_uint16(), // NSCOUNT
					 h_uint16(), // ARCOUNT
					 NULL);

  const HParser *type = h_int_range(h_uint16(), 1, 16);

  const HParser *qtype = h_choice(type, 
				  h_int_range(h_uint16(), 252, 255),
				  NULL);

  const HParser *class = h_int_range(h_uint16(), 1, 4);
  
  const HParser *qclass = h_choice(class,
				   h_int_range(h_uint16(), 255, 255),
				   NULL);

  const HParser *dns_question = h_sequence(h_sequence(h_many1(h_length_value(h_uint8(), 
									     h_uint8())), 
						      h_ch('\x00'),
						      NULL),  // QNAME
					   qtype,           // QTYPE
					   qclass,          // QCLASS
					   NULL);
 

  const HParser *dns_rr = h_sequence(domain,                          // NAME
				     type,                            // TYPE
				     class,                           // CLASS
				     h_uint32(),                        // TTL
				     h_length_value(h_uint16(), h_uint8()), // RDLENGTH+RDATA
				     NULL);


  dns_message = (HParser*)h_attr_bool(h_sequence(dns_header,
						 h_many(dns_question),
						 h_many(dns_rr),
						 h_end_p(),
						 NULL),
				      validate_dns);

  return dns_message;
}

int main(int argc, char** argv) {
  return 0;
}